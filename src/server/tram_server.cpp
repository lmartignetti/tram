#include "server/tram_server.hpp"

#include <cmath>
#include <iostream>
#include <map>
#include <new>
#include <sched.h>
#include <thread>

#include "datatypes.hpp"
#include "logging.hpp"
#include "mem_v.hpp"
#include "server/remote_child.hpp"
#include "server/remote_client.hpp"
#include "server/remote_parent.hpp"
#include "server/remote_replica.hpp"
#include "structures/hm_slot_mask.hpp"
#include "structures/l_slot_mask.hpp"

tram_server::tram_server(smcluster &cluster, const tram_config conf) : tram_process(cluster, conf), last_served_client_idx(0) {

    // Compute redundant variables used to speed up

    // Find current group id
    this->gid = -1;
    for (size_t gid = 0; gid < this->groups.size(); gid++)
        for (int p : this->groups.at(gid))
            if (p == this->cluster.get_pid()) {
                CHECK(this->gid == -1, "A process cannot be server of multiple groups")
                this->gid = gid;
            }
    CHECK(this->gid != -1, "A server must be part of a group")

    // Client processes
    std::vector<int> client_ids;
    for (size_t pid = 0; pid < this->cluster.get_num_processes(); pid++) {
        bool found = false;
        for (std::vector<int> g : this->groups)
            for (int p : g)
                if (p == pid)
                    found = true;
        if (!found)
            client_ids.push_back(pid);
    }
    this->clients.reserve(client_ids.size());
    for (int client_id : client_ids)
        this->clients.emplace_back(client_id, *this);

    // Replica processes
    this->replicas.reserve(this->groups.at(this->gid).size() - 1);
    for (int pid : this->groups.at(this->gid)) {
        if (pid == this->cluster.get_pid())
            continue;
        this->replicas.emplace_back(pid, *this);
    }

    // Child groups
    std::vector<unsigned int> children_gids = this->overlay_tree.get_children(this->gid);
    this->children.reserve(children_gids.size());
    for (size_t child_idx = 0; child_idx < children_gids.size(); child_idx++) {
        unsigned int child_gid = children_gids.at(child_idx);
        std::vector<int> child_group = this->groups.at(child_gid);
        this->children.emplace_back(child_gid, child_group, *this);
    }

    // Check that the log is big enough to always find a recyclable slot
    CHECK(this->conf.log_len > this->clients.size() + 1,
          "The log size must be at least " + std::to_string(this->clients.size() + 2) + " but it is " + std::to_string(this->conf.log_len))

    // Consensus
    this->rnd = 1;
    this->FUO = 0;

    this->next_seq_l = 1;

    // Create the log
    size_t l_size = l_slot_mask::get_size(this->groups.size(), this->alignment, this->conf.payload_size);
    this->LOG = new (std::align_val_t(alignment))  unsigned char[this->conf.log_len * l_size]();
    for (size_t slot_idx = 0; slot_idx < this->conf.log_len; slot_idx++)
        this->LOG_slots.emplace_back(this->LOG + slot_idx * l_size, this->groups.size(), this->alignment, this->conf.payload_size);

    // Create header buffer for the parent
    this->server_buffer_size = this->clients.size() * this->conf.pending_len;
    LOG_INFO("Parent buffer entries: " << this->server_buffer_size)
    this->parent_group = this->overlay_tree.get_parent(this->gid);
    if (this->parent_group != -1)
        this->parent = std::make_unique<remote_parent>(this->parent_group, this->groups.at(this->parent_group), *this);

    // Share the variables
    std::vector<local_shared_variable *> shared;

    // Share the client buffers with the clients
    for (size_t client_idx = 0; client_idx < this->clients.size(); client_idx++)
        this->clients.at(client_idx).reg_local_vars(shared);

    // Share the LOG, FUO and the permission slots/acks with the other servers of the current group
    for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++)
        this->replicas.at(replica_idx).reg_local_vars(shared);

    // Share the parent buffer with the parent
    if (this->parent_group != -1)
        this->parent.get()->reg_local_vars(shared);

    cluster.share_variables(shared);
    std::vector<std::vector<remote_shared_variable *>> remote_variables = cluster.get_remote_variables();

    // Each client shared a sequence of acks
    for (size_t client_idx = 0; client_idx < this->clients.size(); client_idx++) {
        remote_client &client = this->clients.at(client_idx);
        client.store_remote_vars(remote_variables.at(client.get_id()));
    }

    // Each other server of the group shared the LOG and the permission slot/ack
    for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++) {
        remote_replica &replica = this->replicas.at(replica_idx);
        replica.store_remote_vars(remote_variables.at(replica.get_id()));
    }

    // Each child server shared the header buffer (both full and slotted)
    for (size_t child_idx = 0; child_idx < this->children.size(); child_idx++) {
        remote_child &child = this->children.at(child_idx);

        std::map<int, std::vector<remote_shared_variable *>> child_vars;
        for (int pid : child.get_group_ids())
            child_vars[pid] = remote_variables.at(pid);

        child.store_remote_vars(child_vars);
    }

    this->leader = -1;

    // Revoke permissions to access the LOG
    LOG_INFO("Revoking permissions to access the LOG to all replicas")
    // TODO: uncomment this if permissions are enabled
    // for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++)
    //     this->replicas.at(replica_idx).revoke_permissions();

    LOG_INFO("Revoking permissions done")

    // Allocate confirmed followers
    for (size_t i = 0; i < this->groups.at(gid).size() / 2; i++)
        // this->confirmed_followers_idxs.push_back(SIZE_MAX);
        this->confirmed_followers_idxs.push_back(i);

    // this->running_recovery_thread.store(true);
    // std::atomic_bool ready_recovery_thread = false;
    // this->recovery_thread = std::thread(&tram_server::recovery_thr, this, std::ref(ready_recovery_thread));
    // while (!ready_recovery_thread.load())
    //     ;
    LOG_INFO("Recovery thread ready")

    this->running_server.store(true);
    this->server_thread = std::thread(&tram_server::server_thr, this);

    LOG_INFO("Synchronizing...")
    this->cluster.synchronize_all();

    // if (this->cluster.get_pid() == this->groups.at(this->gid).at(0))
    // lead();
    this->leader = this->groups.at(this->gid).at(0); // TODO: remove hardcoded

    this->last_ping_time = std::chrono::high_resolution_clock::now();
    this->last_seq_l_ping = -this->ping_delta;
    LOG_INFO("Server constructor done")

    pin_thread_to_core(0);
}

tram_server::~tram_server() {
    this->cluster.synchronize_all();
    LOG_INFO("Tram server destructor")

    this->running_server.store(false);
    this->server_thread.join();

    // this->running_recovery_thread.store(false);
    // this->recovery_thread.join();

    LOG_INFO("LOG")
    LOG_INFO("<     rnd,    seq_s,        x, <<       c,    seq_c>,      dst,     vptr,    seq_x>>")
    for (size_t slot = 0; slot < this->conf.log_len; slot++)
        LOG_INFO(this->LOG_slots.at(slot).to_string())

    ::operator delete[]((unsigned char *)this->LOG, std::align_val_t(this->alignment));
}

void tram_server::server_thr() {
    pin_thread_to_core(1);

#ifdef DEBUG
    uint64_t timeout = 1; // s
    bool timeout_passed = false;
    bool entry_found = true;
    auto start_stale_search = std::chrono::high_resolution_clock::now();
#endif

    while (this->leader == -1)
        this->check_perm_requests();

    LOG_INFO("First leader: " << this->leader)

    while (this->running_server.load()) {
    abort:
        this->lead_mutex.lock();

        // Check all processes in the current group since the old leader may have pending operations
        // if (this->leader != this->cluster.get_pid() && this->leader != -1) // TODO: check if || instead of &&
        //     this->check_recovery();

        // this->check_perm_requests();

        if (this->leader != this->cluster.get_pid()) {
            // this->follower();
            this->lead_mutex.unlock();
            continue;
        }

#ifdef DEBUG
        if (entry_found) {
            start_stale_search = std::chrono::high_resolution_clock::now();
            entry_found = false;
            timeout_passed = false;
        }
#endif
        entry_type source; // 0 <= source <= num_clients
        size_t active_slot_idx = this->find_active_slot(source);
        if (active_slot_idx == -1) {
#ifdef DEBUG
            auto current_stale_search = std::chrono::high_resolution_clock::now();
            auto delta = std::chrono::duration_cast<std::chrono::seconds>(current_stale_search - start_stale_search).count();
            if (!timeout_passed && delta > timeout) {
                timeout_passed = true;
                LOG_INFO(this->to_string())
            }
#endif
            this->lead_mutex.unlock();
            continue;
        }
#ifdef DEBUG
        entry_found = true;
#endif

        hm_slot_mask &active_hm_slot = source == this->clients.size() ? this->parent.get()->get_hm_slot_at(active_slot_idx)
                                                                      : this->clients.at(source).get_hm_slot_at(active_slot_idx);

        // Retrieve the active client from the client id
        entry_type active_client_pid = active_hm_slot.get_h_slot().get_msg_id().pid;
        entry_type active_client_idx;
        for (active_client_idx = 0; this->clients.at(active_client_idx).get_id() != active_client_pid; active_client_idx++)
            ;

        if (!this->task1(active_hm_slot, source, active_client_idx)) {
            this->lead_mutex.unlock();
            this->leader = -1;
            goto abort;
        }

        // DELIVERY HERE

        this->task2(active_hm_slot);
        this->task3(active_hm_slot, active_client_idx);

        this->lead_mutex.unlock();

        LOG_INFO("Done" << std::endl)
    }
}

std::string tram_server::to_string() {
    std::stringstream ss;
    ss << "Tram server state" << std::endl;
    for (size_t client_idx = 0; client_idx < this->clients.size(); client_idx++)
        ss << this->clients.at(client_idx).to_string() << std::endl;
    if (this->parent_group != -1)
        ss << this->parent.get()->to_string() << std::endl;
    for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++)
        ss << this->replicas.at(replica_idx).to_string();
    for (size_t child_idx; child_idx < this->children.size(); child_idx++)
        ss << this->children.at(child_idx).to_string() << std::endl;

    return ss.str();
}
