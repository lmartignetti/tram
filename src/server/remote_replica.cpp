#include "server/remote_replica.hpp"

#include <iostream>

#include "logging.hpp"
#include "mem_v.hpp"
#include "server/remote_client.hpp"
#include "structures/l_slot_mask.hpp"

tram_server::remote_replica::remote_replica(int id, const tram_server &server) : tram_process(server), server(server), id(id) {
    this->perm = new (std::align_val_t(alignment))  unsigned char[sizeof(entry_type)]();
    this->perm_ack = new (std::align_val_t(alignment))  unsigned char[sizeof(entry_type)]();

    this->suspected = new std::atomic_bool;
    this->suspected->store(false);
}

tram_server::remote_replica::~remote_replica() {
    LOG_INFO("Destruction of replica " << this->id)

    ::operator delete[]((unsigned char *)this->perm, std::align_val_t(this->server.alignment));
    ::operator delete[]((unsigned char *)this->perm_ack, std::align_val_t(this->server.alignment));

    delete this->suspected;
}

void tram_server::remote_replica::reg_local_vars(std::vector<local_shared_variable *> &shared) {
    shared.push_back(cluster.reg_local_var(this->perm, sizeof(entry_type), {this->id}));
    shared.push_back(cluster.reg_local_var(this->perm_ack, sizeof(entry_type), {this->id}));

    shared.push_back(cluster.reg_local_var(( void *)&this->server.rnd, sizeof(entry_type), {this->id}));
    shared.push_back(cluster.reg_local_var(( void *)&this->server.FUO, sizeof(entry_type), {this->id}));
    size_t l_size = l_slot_mask::get_size(this->groups.size(), this->alignment, this->conf.payload_size);
    this->l_LOG = cluster.reg_local_var(( void *)this->server.LOG, this->server.conf.log_len * l_size, {this->id});
    shared.push_back(this->l_LOG);
    for (size_t slot_idx = 0; slot_idx < this->server.conf.log_len; slot_idx++) {
        this->l_LOG_slots.push_back(cluster.reg_local_var(this->server.LOG + slot_idx * l_size, l_size, {this->id}));
        shared.push_back(this->l_LOG_slots.back());
    }
}

void tram_server::remote_replica::store_remote_vars(const std::vector<remote_shared_variable *> &remote_vars) {
    size_t exp_remote_vars_size = 5 + this->server.conf.log_len;
    CHECK(remote_vars.size() == exp_remote_vars_size, "Got " + std::to_string(remote_vars.size()) + " remote variables instead of " +
                                                          std::to_string(exp_remote_vars_size) + " from replica " + std::to_string(this->id))
    LOG_INFO("Replica " << this->id << " shared " << remote_vars.size() << " variables")

    auto check_size = [](remote_shared_variable *rem_var, const std::string &var_name, size_t exp_size) -> void {
        CHECK(rem_var->get_size() == exp_size,
              "Variable size of " + var_name + " is " + std::to_string(rem_var->get_size()) + " instead of " + std::to_string(exp_size));
    };

    check_size(remote_vars.at(0), "perm", sizeof(entry_type));
    this->r_perm = remote_vars.at(0);
    *( entry_type *)(this->r_perm->get_address()) = 0;
    check_size(remote_vars.at(1), "perm_ack", sizeof(entry_type));
    this->r_perm_ack = remote_vars.at(1);
    *( entry_type *)(this->r_perm_ack->get_address()) = 0;

    check_size(remote_vars.at(2), "rnd", sizeof(entry_type));
    this->r_rnd = remote_vars.at(2);
    *( entry_type *)(this->r_rnd->get_address()) = 0;
    check_size(remote_vars.at(3), "FUO", sizeof(entry_type));
    this->r_FUO = remote_vars.at(3);
    *( entry_type *)(this->r_FUO->get_address()) = 0;
    size_t l_size = l_slot_mask::get_size(this->groups.size(), this->alignment, this->conf.payload_size);
    check_size(remote_vars.at(4), "LOG", this->server.conf.log_len * l_size);
    this->r_LOG = remote_vars.at(4);
    memset_v(this->r_LOG->get_address(), 0, this->server.conf.log_len * l_size);
    for (size_t slot_idx = 0; slot_idx < this->server.conf.log_len; slot_idx++) {
        check_size(remote_vars.at(5 + slot_idx), "LOG slot " + std::to_string(slot_idx), l_size);
        this->r_LOG_slots.push_back(remote_vars.at(5 + slot_idx));
    }
}

void tram_server::remote_replica::grant_permissions() {
    this->l_LOG->set_perm_rw(this->id);
    for (size_t slot_idx = 0; slot_idx < this->server.conf.log_len; slot_idx++)
        this->l_LOG_slots.at(slot_idx)->set_perm_rw(this->id);
}

void tram_server::remote_replica::revoke_permissions() {
    this->l_LOG->set_perm_none(this->id);
    for (size_t slot_idx = 0; slot_idx < this->server.conf.log_len; slot_idx++)
        this->l_LOG_slots.at(slot_idx)->set_perm_none(this->id);
}

void tram_server::remote_replica::recover() { this->cluster.recovery(this->id); }

bool tram_server::remote_replica::is_active() {
    bool res = this->cluster.is_remote_active(this->id);
    return res;
}

std::string tram_server::remote_replica::to_string() const {
    std::stringstream ss;
    ss << "Remote replica process " << this->id << " state" << std::endl;
    ss << "- suspected: " << this->suspected->load(std::memory_order_relaxed) << std::endl;

    return ss.str();
}
