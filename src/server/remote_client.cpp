#include "server/remote_client.hpp"

#include <iostream>

#include "logging.hpp"
#include "mem_v.hpp"
#include "structures/hm_slot_mask.hpp"

tram_server::remote_client::remote_client(int id, const tram_server &server) : tram_process(server), server(server), id(id) {
    size_t hm_slot_size = hm_slot_mask::get_size(server.groups.size(), server.alignment, server.conf.payload_size);

    for (size_t slot_idx = 0; slot_idx < this->conf.pending_len; slot_idx++) {
         unsigned char *ptr = new (std::align_val_t(this->server.alignment))  unsigned char[hm_slot_size]();
        this->buffer.emplace_back(ptr, server.groups.size(), server.alignment, server.conf.payload_size);
    }
    this->next_seq_h = 1;

    this->last_l_slot_idx = -1;

    this->next_ack = 1;
}

tram_server::remote_client::~remote_client() {
    for (size_t slot_idx = 0; slot_idx < this->conf.pending_len; slot_idx++)
        ::operator delete[]((unsigned char *)this->buffer.at(slot_idx).get_m_slot().get_address(), std::align_val_t(this->server.alignment));
}

void tram_server::remote_client::reg_local_vars(std::vector<local_shared_variable *> &shared) {
    for (size_t slot_idx = 0; slot_idx < this->conf.pending_len; slot_idx++)
        shared.push_back(cluster.reg_local_var(
            this->buffer.at(slot_idx).get_address(),
            hm_slot_mask::get_size(this->server.groups.size(), this->server.alignment, this->server.conf.payload_size), {this->id}));
}

void tram_server::remote_client::store_remote_vars(std::vector<remote_shared_variable *> remote_vars) {
    CHECK(remote_vars.size() == this->conf.pending_len, "Remote client " + std::to_string(this->id) + " shared " +
                                                            std::to_string(remote_vars.size()) + " variables instead of " +
                                                            std::to_string(this->conf.pending_len))
    LOG_INFO("Client " << this->id << " shared " << remote_vars.size() << " variables")

    for (size_t slot_idx = 0; slot_idx < this->conf.pending_len; slot_idx++) {
        CHECK(remote_vars.at(slot_idx)->get_size() == sizeof(entry_type), "Variable size of client ack at slot " + std::to_string(slot_idx) + " is " +
                                                                              std::to_string(remote_vars.at(slot_idx)->get_size()) + " instead of " +
                                                                              std::to_string(sizeof(entry_type)))
        this->r_acks.push_back(remote_vars.at(slot_idx));
    }
}

ssize_t tram_server::remote_client::check_active_hm_slot(entry_type &seq_h) {
    for (size_t slot_idx = 0; slot_idx < this->conf.pending_len; slot_idx++) {
        hm_slot_mask &hm_slot = this->buffer.at(slot_idx);
        entry_type seq = hm_slot.get_h_slot().get_seq();
        if (seq == this->next_seq_h) {
            LOG_INFO("Processing client " << this->id << " H entry (seq: " << std::hex << seq << std::dec << ") at slot " << slot_idx << ": "
                                          << hm_slot.to_string())
            seq_h = this->next_seq_h++;
            return slot_idx;
        }
    }

    return -1;
}

entry_type tram_server::remote_client::read_ack(size_t slot_idx) {
    CHECK(this->r_acks.at(slot_idx)->read(), "Read from client ack failed for client " + std::to_string(this->id));
    entry_type ack = *( entry_type *)this->r_acks.at(slot_idx)->get_address();
    LOG_INFO("Read ack from client " << this->id << " at slot " << slot_idx << ": 0x" << std::hex << ack << std::dec)
    return ack;
}

void tram_server::remote_client::write_ack(size_t slot_idx) {
    *( entry_type *)this->r_acks.at(slot_idx)->get_address() = this->next_ack;
    CHECK(this->r_acks.at(slot_idx)->write(), "Write to client ack failed for client " + std::to_string(this->id));
    LOG_INFO("Written ack to client " << this->id << " at slot " << slot_idx << ": 0x" << std::hex << this->next_ack << std::dec)
    this->next_ack++;
}

std::string tram_server::remote_client::to_string() const {
    std::stringstream ss;
    ss << "Remote client process " << this->id << " state" << std::endl;
    ss << "- next_seq_h: " << this->next_seq_h << std::endl;
    ss << "- last_l_slot_idx: " << this->last_l_slot_idx << std::endl;
    ss << "- next_ack: " << this->next_ack << std::endl;
    ss << "Buffer" << std::endl;
    for (size_t slot_idx = 0; slot_idx < this->buffer.size(); slot_idx++)
        ss << "Slot " << slot_idx << ": " << this->buffer.at(slot_idx).to_string() << std::endl;

    return ss.str();
}