#include "server/tram_server.hpp"

#include "logging.hpp"
#include "server/remote_child.hpp"
#include "server/remote_client.hpp"
#include "server/remote_parent.hpp"
#include "server/remote_replica.hpp"
#include "structures/hm_slot_mask.hpp"
#include "structures/l_slot_mask.hpp"

// IDEA: use a heartbeat just to check if a follower can read from the leader. If not, suspect the leader.

struct replica_to_recover {
    int pid;
    std::atomic_bool *done;
    std::thread *th;
};

void tram_server::recover_remote(remote_replica *replica, std::atomic_bool *done) {
    replica->recover();
    done->store(true);
}

void tram_server::recovery_thr(std::atomic_bool &ready) {
    LOG_INFO("Started recovery thread")

    pin_thread_to_core(2);

    std::vector<replica_to_recover> to_recover;
    bool locked = false;

    // Initialize
    for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++) {
        remote_replica &replica = this->replicas.at(replica_idx);
        to_recover.push_back({replica.get_id(), new std::atomic_bool, nullptr});
        to_recover.back().done->store(true);
    }

    ready.store(true);
    do {
        // As leader, ping if I am too slow

        auto current_time = std::chrono::high_resolution_clock::now();
        uint64_t delta = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - this->last_ping_time).count();

        if (delta > this->ping_delta) {
            ssize_t speed = this->next_seq_l - this->last_seq_l_ping;
            if (this->leader == this->cluster.get_pid() && speed < this->ping_delta) {
                LOG_INFO("Leader is slow (" << speed << ") so we ping replicas")
                if (!locked) {
                    LOG_INFO("Locking...")
                    this->lead_mutex.lock();
                }

                this->replicas_ping();
                this->check_perm_requests();
                this->last_ping_time = std::chrono::high_resolution_clock::now();
                LOG_INFO("Replicas pinged")

                if (!locked) {
                    LOG_INFO("Unlocking...")
                    this->lead_mutex.unlock();
                }
            }
            this->last_seq_l_ping = this->next_seq_l;
        }

        // Scan for replicas to recover
        for (size_t replica_idx = 0; replica_idx < to_recover.size(); replica_idx++) {
            replica_to_recover &rec = to_recover.at(replica_idx);
            remote_replica &replica = this->replicas.at(replica_idx);
            if (replica.get_suspected() && rec.th == nullptr && rec.done->load()) {
                if (!locked) {
                    LOG("Recovery mode starts (replica " << rec.pid << " suspected): locking...")
                    LOG_INFO("Recovery mode starts (replica " << rec.pid << " suspected): locking...")
                    locked = true;
                    this->lead_mutex.lock();
                }
                if (!this->cluster.is_remote_active(rec.pid)) {
                    LOG_INFO("Calling recovery for remote: " << rec.pid)
                    rec.done->store(false);
                    rec.th = new std::thread(&tram_server::recover_remote, this, &replica, rec.done);
                }
            }
        }

        // Check if some is done
        bool all_done = true;
        for (size_t replica_idx = 0; replica_idx < to_recover.size(); replica_idx++) {
            replica_to_recover &rec = to_recover.at(replica_idx);
            if (rec.th != nullptr) {
                all_done = false;
                if (rec.done->load()) {
                    LOG_INFO("Joining recovery thread for remote: " << rec.pid)
                    rec.th->join();
                    delete rec.th;
                    rec.th = nullptr;
                    LOG_INFO("Recovery successful for remote: " << rec.pid)
                    this->replicas.at(replica_idx).set_suspected(false);
                }
            }
        }

        if (locked && all_done) {
            LOG_INFO("Recovery mode ends: unlocking...")
            this->lead_mutex.unlock();
            locked = false;
        }
    } while (this->running_recovery_thread.load());

    // Cleanup
    if (locked) {
        this->lead_mutex.unlock();
    }

    for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++)
        delete to_recover.at(replica_idx).done;

    LOG_INFO("Recovery thread done")
}

bool tram_server::failover_task1() {
    LOG_INFO("Failover task 1: revoke old leader permissions and gather permissions from a quorum of replicas")

    // Revoke old leader permissions
    if (this->leader != -1) {
        LOG_INFO("Revoking permissions to the old leader: " << this->leader)
        for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++)
            if (this->replicas.at(replica_idx).get_id() == this->leader)
                // TODO: fix with permissions
                // this->replicas.at(replica_idx).revoke_permissions();
                ;
        this->leader = -1;
    }

    // Request acknowledgements to all processes in the group
    for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++) {
        remote_replica &replica = this->replicas.at(replica_idx);
        if (!replica.write_perm(this->next_seq_l)) {
            LOG_INFO("Ack " << this->next_seq_l << " request to process " << std::to_string(replica.get_id()) << " failed")
            replica.set_suspected(true);
            this->lead_mutex.unlock();
            this->lead_mutex.lock();
            replica_idx--;
            continue;
        }
        LOG_INFO("Ack " << this->next_seq_l << " requested to process " << std::to_string(replica.get_id()))
    }
    this->lead_mutex.unlock();

    // Collect a quorum of confirmed followers: this must be interleaved with follower logic to avoid deadlock when all processes are waiting acks
    for (size_t cf_idx = 0; cf_idx < this->confirmed_followers_idxs.size(); cf_idx++) {
        bool found = false;
        while (!found) {
            this->lead_mutex.lock();
            if (this->leader != -1) {
                LOG_INFO("Leader changed while waiting for permissions acks: leading failed")
                return false;
            }

            for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++) {
                remote_replica &replica = this->replicas.at(replica_idx);
                entry_type replica_perm_ack = replica.get_perm_ack();
                if (replica_perm_ack == this->next_seq_l) {
                    replica.reset_perm_ack();
                    this->confirmed_followers_idxs.at(cf_idx) = replica_idx;
                    LOG_INFO("Found ack " << replica_perm_ack << " from replica " << replica.get_id());
                    found = true;
                    break;
                }
            }
            this->lead_mutex.unlock();
        }
    }

    this->lead_mutex.lock();
    if (this->leader != -1) {
        LOG_INFO("Leader changed while waiting for permissions acks: leading failed")
        return false;
    }
    LOG_INFO("Permissions gathered correctly" << std::endl);
    LOG_INFO("Confirmed followers idxs: " << vector_to_string(this->confirmed_followers_idxs) << " (replicas: " << this->replicas.size() << ")");

    return true;
}

bool tram_server::failover_task2() {
    LOG_INFO("Failover task 2: leader catchup (recover rnd, FUO and LOG)")
    size_t l_size = l_slot_mask::get_size(this->groups.size(), this->alignment, this->conf.payload_size);

    // Read the remote LOGs of all the followers
    for (int replica_idx : this->confirmed_followers_idxs) {
        remote_replica &follower = this->replicas.at(replica_idx);
        if (!follower.get_LOG()->read()) {
            LOG("Could not read LOG from follower " << follower.get_id() << ": leading failed")
            LOG_INFO("Could not read LOG from follower " << follower.get_id() << ": leading failed")
            follower.set_suspected(true);
            return false;
        }
        LOG_INFO("Read LOG from follower " << follower.get_id() << ":")
        for (size_t slot_idx = 0; slot_idx < this->conf.log_len; slot_idx++) {
            memcpy_v(follower.get_LOG_slot_at(slot_idx)->get_address(), follower.get_LOG()->get_address() + slot_idx * l_size, l_size);
            LOG_INFO("Slot " << slot_idx << ": "
                             << l_slot_mask(follower.get_LOG_slot_at(slot_idx)->get_address(), this->groups.size(), this->alignment,
                                            this->conf.payload_size)
                                    .to_string())
        }
    }
    LOG("Read LOGs of the followers")
    LOG_INFO("Read LOGs of the followers")

    // Update each local slot with the one among the remote ones with max seq_s
    // Update seq_s and FUO
    this->next_seq_l = 0;
    this->FUO = 0;
    for (size_t slot_idx = 0; slot_idx < this->conf.log_len; slot_idx++) {
        for (int replica_idx : this->confirmed_followers_idxs) {
            remote_replica &follower = this->replicas.at(replica_idx);
            // Overwrite the local one if the remote is newer
            if (l_slot_mask(follower.get_LOG_slot_at(slot_idx)->get_address(), this->groups.size(), this->alignment, this->conf.payload_size)
                    .get_seq_l() > this->LOG_slots.at(slot_idx).get_seq_l())
                // if (this->LOG_slots_remote.at(pid_idx).at(slot)->get_seq_s() > this->LOG_slots.at(slot)->get_seq_s())
                memcpy_v(this->LOG_slots.at(slot_idx).get_address(), follower.get_LOG_slot_at(slot_idx)->get_address(), l_size);
        }
        // Update the next seq_l
        if (this->LOG_slots.at(slot_idx).get_seq_l() > this->next_seq_l) {
            this->next_seq_l = this->LOG_slots.at(slot_idx).get_seq_l();
            this->FUO = (slot_idx + 1) % this->conf.log_len;
        }
        LOG_INFO("Local LOG slot " << slot_idx << ": " << this->LOG_slots.at(slot_idx).to_string())
    }
    this->next_seq_l++;

    // Update the LOGs of the followers
    for (int replica_idx : this->confirmed_followers_idxs) {
        remote_replica &follower = this->replicas.at(replica_idx);
        memcpy_v(follower.get_LOG()->get_address(), this->LOG, this->conf.payload_size * l_size);
        LOG("HERE4")
        if (!follower.get_LOG()->write()) {
            LOG_INFO("Could not write LOG to follower " << follower.get_id() << ": leading failed")
            LOG("Could not write LOG to follower " << follower.get_id() << ": leading failed")
            follower.set_suspected(true);
            return false;
        }
    }
    LOG("Written LOGs of the followers")
    LOG_INFO("Written LOGs of the followers")

    // Read rnd from followers and pick a bigger one
    for (int replica_idx : this->confirmed_followers_idxs) {
        remote_replica &follower = this->replicas.at(replica_idx);
        if (!follower.get_rnd()->read()) {
            LOG_INFO("Could not read rnd from follower " << follower.get_id() << ": leading failed")
            follower.set_suspected(true);
            return false;
        }
        entry_type rnd_temp = *( entry_type *)(follower.get_rnd()->get_address());
        LOG_INFO("Read rnd " << rnd_temp << " from follower " << follower.get_id())
        if (rnd_temp > this->rnd)
            this->rnd = rnd_temp;
    }
    this->rnd++;
    LOG("New rnd: " << this->rnd)
    LOG_INFO("New rnd: " << this->rnd)

    return true;
}

void tram_server::failover_task3() {
    LOG_INFO("Failover task 3: find the last seq for each input source (parent + clients)")

    for (ssize_t slot_idx = 0; slot_idx < this->LOG_slots.size(); slot_idx++) {
        l_slot_mask &l_slot = this->LOG_slots.at(slot_idx);

        // Skip empty LOG slots
        if (l_slot.get_seq_l() <= 0)
            continue;

        // parent source
        if (l_slot.get_source() == this->clients.size()) {
            remote_parent *parent = this->parent.get();
            size_t l_slot_seq_h = l_slot.get_hm_slot().get_h_slot().get_seq();
            if (l_slot_seq_h >= parent->get_next_seq_h() - 1) {
                parent->set_next_seq_h(l_slot_seq_h + 1);
                parent->set_last_l_slot_idx(slot_idx);
            }
        }
        // client source
        else {
            remote_client &client = this->clients.at(l_slot.get_source());
            size_t l_slot_seq_h = l_slot.get_hm_slot().get_h_slot().get_seq();
            if (l_slot_seq_h >= client.get_next_seq_h() - 1) {
                client.set_next_seq_h(l_slot_seq_h + 1);
                client.set_last_l_slot_idx(slot_idx);
            }
        }
    }

    // Update parent buffer head if needed (parent buffer is filled sequentially)
    if (this->parent_group != -1) {
        remote_parent *parent = this->parent.get();
        size_t last_seq_h = parent->get_next_seq_h() - 1;
        if (last_seq_h > 0) {
            for (size_t slot_idx = 0; slot_idx < parent->get_buffer_len(); slot_idx++) {
                hm_slot_mask &hm_slot = parent->get_hm_slot_at(slot_idx);
                if (hm_slot.get_h_slot().get_seq() == last_seq_h) {
                    size_t buffer_head = (slot_idx + 1) % parent->get_buffer_len();
                    parent->set_buffer_head(buffer_head);
                    LOG_INFO("Parent buffer head: " << buffer_head)
                    break;
                }
            }
        }
    }

    // Log results
    if (this->parent_group != -1) {
        LOG_INFO(this->parent.get()->to_string())
    }
    for (size_t client_idx = 0; client_idx < this->clients.size(); client_idx++) {
        LOG_INFO(this->clients.at(client_idx).to_string())
    }
}

void tram_server::failover_task4() {
    LOG_INFO("Failover task 4: find the last client ack for each client")

    for (size_t client_idx = 0; client_idx < this->clients.size(); client_idx++) {
        remote_client &client = this->clients.at(client_idx);
        entry_type last_ack = 0;
        for (size_t slot_idx = 0; slot_idx < this->conf.pending_len; slot_idx++) {
            entry_type slot_ack = client.read_ack(slot_idx);
            if (slot_ack > last_ack)
                last_ack = slot_ack;
        }
        client.set_next_ack(last_ack + 1);
    }

    // Log results
    for (size_t client_idx = 0; client_idx < this->clients.size(); client_idx++) {
        remote_client &client = this->clients.at(client_idx);
        LOG_INFO(client.to_string())
    }
}

std::vector<timestamp> tram_server::failover_task5() {
    LOG_INFO("Failover task 5: find the buffer head and seq for each child")

    std::vector<timestamp> last_msg_ids; // for each child
    for (size_t child_idx = 0; child_idx < this->children.size(); child_idx++) {
        remote_child &child = this->children.at(child_idx);
        last_msg_ids.push_back(child.recover());
    }

    // Log results
    for (size_t child_idx = 0; child_idx < this->children.size(); child_idx++) {
        remote_child &child = this->children.at(child_idx);
        LOG_INFO(child.to_string())
        LOG_INFO("Child " << child.get_gid() << " last msg id: " << ts_to_string(last_msg_ids.at(child_idx)))
    }

    // Return the last msg id for each child
    return last_msg_ids;
}

void tram_server::failover_task6(std::vector<timestamp> last_msg_ids) {
    LOG_INFO("Failover task 6: manage the last log entry: resend client ack (if in dst) and rewrite on children")

    // Find the last LOG entry
    entry_type seq_l_max = 0;
    size_t slot_idx_seq_l_max = 0;
    for (size_t slot_idx = 0; slot_idx < this->conf.log_len; slot_idx++) {
        l_slot_mask &l_slot = this->LOG_slots.at(slot_idx);
        if (l_slot.get_seq_l() > seq_l_max) {
            seq_l_max = l_slot.get_seq_l();
            slot_idx_seq_l_max = slot_idx;
        }
    }
    l_slot_mask &last_l_slot = this->LOG_slots.at(slot_idx_seq_l_max);
    hm_slot_mask &active_hm_slot = last_l_slot.get_hm_slot();
    LOG_INFO("Last LOG slot is " << slot_idx_seq_l_max << " (seq " << seq_l_max << "): " << last_l_slot.to_string())

    // Forward to children if needed
    for (size_t child_idx = 0; child_idx < this->children.size(); child_idx++) {
        remote_child &child = this->children.at(child_idx);
        unsigned int child_gid = child.get_gid();
        timestamp last_child_msg_id = last_msg_ids.at(child_idx);

        for (unsigned int group_in_reach : this->overlay_tree.get_reach(child_gid)) {
            if (last_l_slot.get_hm_slot().get_h_slot().is_dst(group_in_reach)) {
                timestamp last_l_msg_id = last_l_slot.get_hm_slot().get_h_slot().get_msg_id();

                // Break if already written
                if (last_l_msg_id.pid == last_child_msg_id.pid && last_l_msg_id.counter == last_child_msg_id.counter) {
                    LOG_INFO("Last msg id " << ts_to_string(last_l_msg_id) << " for child " << child_gid << " has already been written")
                    break;
                }

                // Re-write
                LOG_INFO("Re-writing last msg id " << ts_to_string(last_l_msg_id) << " on child " << child_gid)
                child.write_next_hm_slot(active_hm_slot);
                break;
            }
        }
    }

    // Re-write the client ack if needed
    if (active_hm_slot.get_h_slot().is_dst(this->gid)) {
        // Retrieve the active client from the client id
        entry_type active_client_pid = active_hm_slot.get_h_slot().get_msg_id().pid;
        entry_type active_client_idx;
        for (active_client_idx = 0; this->clients.at(active_client_idx).get_id() != active_client_pid; active_client_idx++)
            ;
        remote_client &active_client = this->clients.at(active_client_idx);

        // Decrement the next ack by one to rewrite the correct one
        active_client.set_next_ack(last_l_slot.get_ack_c());
        this->task3(active_hm_slot, active_client_idx);
    }
}

bool tram_server::lead() {
    CHECK(this->leader != this->cluster.get_pid(), "Cannot lead if I am the current leader")
    LOG_INFO("Trying to lead group " << this->gid << " made of " << this->groups.at(this->gid).size() << " processes")

    this->lead_mutex.lock();
    LOG_INFO("Locked")

    if (!this->failover_task1()) {
        this->lead_mutex.unlock();
        return false;
    }

    if (!this->failover_task2()) {
        this->lead_mutex.unlock();
        return false;
    }
    this->failover_task3();
    this->failover_task4();
    this->failover_task6(this->failover_task5());

    this->leader = this->cluster.get_pid();
    LOG_INFO("Current process is now the leader of the group")

    this->lead_mutex.unlock();
    LOG_INFO("Unlocked")

    return true;
}
