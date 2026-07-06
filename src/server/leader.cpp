#include "server/remote_child.hpp"
#include "server/remote_client.hpp"
#include "server/remote_parent.hpp"
#include "server/remote_replica.hpp"
#include "server/tram_server.hpp"
#include "structures/hm_slot_mask.hpp"
#include "structures/l_slot_mask.hpp"

size_t tram_server::find_active_slot(entry_type &source) {
    entry_type seq_h;

    // Check the parent's header buffer
    if (this->parent_group != -1) {
        ssize_t active_slot_idx = this->parent.get()->check_active_slot_hm(seq_h);
        if (active_slot_idx != -1) {
            source = this->clients.size();
            return active_slot_idx;
        }
    }

    // Check the clients' header buffer
    for (; this->last_served_client_idx < this->clients.size(); this->last_served_client_idx++) {
        remote_client &client = this->clients.at(this->last_served_client_idx);
        ssize_t active_slot_idx = client.check_active_hm_slot(seq_h);
        if (active_slot_idx != -1) {
            source = this->last_served_client_idx;
            this->last_served_client_idx++;
            return active_slot_idx;
        }
    }

    this->last_served_client_idx = 0;
    return -1;
}

bool tram_server::task1(hm_slot_mask &active_hm_slot, entry_type source, entry_type active_client_idx) {
    h_slot_mask &active_h_slot = active_hm_slot.get_h_slot();
    remote_client &active_client = this->clients.at(active_client_idx);

    // Consensus
    entry_type ack_temp = active_h_slot.is_dst(this->gid) ? active_client.get_next_ack() : 0;

    // Find a recyclable l_slot (move FUO): it must not be the last written for any source
    while (true) {
        l_slot_mask &l_slot = this->LOG_slots.at(this->FUO);
        if (l_slot.get_source() == this->clients.size()) { // parent source
            if (this->parent.get()->get_last_l_slot_idx() != this->FUO) {
                this->parent.get()->set_last_l_slot_idx(this->FUO);
                break;
            }
        } else { // client source
            if (this->clients.at(l_slot.get_source()).get_last_l_slot_idx() != this->FUO) {
                this->clients.at(l_slot.get_source()).set_last_l_slot_idx(this->FUO);
                break;
            }
        }
        this->FUO = (this->FUO + 1) % this->conf.log_len;
    }

    // Replication
    l_slot_mask &l_slot = this->LOG_slots.at(this->FUO);

    l_slot.set_rnd(this->rnd);
    l_slot.set_seq(this->next_seq_l++);
    l_slot.set_ack_c(ack_temp);
    l_slot.set_source(source);
    l_slot.get_hm_slot().set(active_hm_slot);
    std::vector<std::pair<remote_shared_variable *, uint64_t>> pending_ops_replication;
    for (size_t replica_idx : this->confirmed_followers_idxs) {
        remote_replica &replica = this->replicas.at(replica_idx);
        remote_shared_variable *remote_l_slot = replica.get_LOG_slot_at(this->FUO);
        memcpy_v(remote_l_slot->get_address(), l_slot.get_address(), l_slot.get_size());
        uint64_t wr_id = remote_l_slot->write_async();
        pending_ops_replication.push_back({remote_l_slot, wr_id});
        LOG_INFO("Written log entry at follower " << replica.get_id() << " at slot " << this->FUO << ": " << l_slot.to_string() << " (op " << wr_id
                                                  << ")")
    }

    bool lead_change = false;
    for (std::pair<remote_shared_variable *, uint64_t> &pending_op : pending_ops_replication) {
        if (!pending_op.first->wait(pending_op.second)) {
            LOG_WARN("Operation " << pending_op.second << " failed: leader changed" << std::endl)
            lead_change = true;

            // Suspect the replica: the recovery thread will take care of it
            for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++) {
                remote_replica &replica = this->replicas.at(replica_idx);
                if (replica.get_id() == pending_op.first->get_remote()) {
                    replica.set_suspected(true);
                    break;
                }
            }
        }
    }

    if (lead_change)
        return false;
    else {
        this->FUO = (this->FUO + 1) % this->conf.log_len;
        return true;
    }
}

void tram_server::task2(hm_slot_mask &active_hm_slot) {
    // Write to all needed child processes without waiting for acks
    for (size_t child_idx = 0; child_idx < this->children.size(); child_idx++) {
        remote_child &child = this->children.at(child_idx);
        unsigned int child_gid = child.get_gid();
        for (unsigned int group_in_reach : this->overlay_tree.get_reach(child_gid)) {
            if (active_hm_slot.get_h_slot().is_dst(group_in_reach)) {
                child.write_next_hm_slot(active_hm_slot);
                break;
            }
        }
    }

    // Wait for all acks all at once
    for (size_t child_idx = 0; child_idx < this->children.size(); child_idx++) {
        remote_child &child = this->children.at(child_idx);
        child.wait_pending_ops();
    }
}

void tram_server::task3(hm_slot_mask &active_hm_slot, entry_type active_client_idx) {
    remote_client &active_client = this->clients.at(active_client_idx);

    // Ack if the current group is a destination
    if (active_hm_slot.get_h_slot().is_dst(this->gid))
        active_client.write_ack(active_hm_slot.get_h_slot().get_vptr());
}

void tram_server::followers_ping() {
    for (size_t replica_idx : this->confirmed_followers_idxs) {
        remote_replica &follower = this->replicas.at(replica_idx);
        if (!follower.ping()) {
            LOG_WARN("Ping failed for replica " << follower.get_id())
            follower.set_suspected(true);
            this->leader = -1;
        }
    }
}

// /**
//  * @brief Given that active vptr is the active slot, and client_idx is the active client index, check to find acks for the given client at the
//  * given slot. Ack is message counter (id).
//  *
//  * @param active_slot
//  * @param active_client_idx
//  */
// void tram_server::wait_value_acks(h_slot_mask &active_slot, entry_type active_client_idx) {
//   entry_type active_slot_idx = active_slot.get_vptr();
//   entry_type msg_counter = active_slot.get_msg_id().counter;

//   LOG_INFO("Waiting for acks from client " << this->clients.at(active_client_idx).get_id() << " at slot " << active_slot_idx)

// #ifdef DEBUG
//   uint64_t timeout = 1; // s
//   auto start_stale_search = std::chrono::high_resolution_clock::now();
//   bool timeout_passed = false;
// #endif

//   bool found_current = false;
//   std::vector<bool> found_replicas;
//   for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++)
//       found_replicas.push_back(false);
//   size_t count = 0;
//   while (count < this->groups.at(this->gid).size() / 2 + 1) {
// #ifdef DEBUG
//       auto current_stale_search = std::chrono::high_resolution_clock::now();
//       auto delta = std::chrono::duration_cast<std::chrono::seconds>(current_stale_search - start_stale_search).count();
//       if (!timeout_passed && delta > timeout) {
//           timeout_passed = true;
//           LOG_INFO("Acks for client " << this->clients.at(active_client_idx).get_id() << " at slot " << active_slot_idx << " not found after 1s")
//           LOG_INFO(this->to_string())
//       }
// #endif

//       // Check current process
//       if (!found_current && this->clients.at(active_client_idx).get_hm_slot_at(active_slot_idx).get_m_slot().get_msg_id().counter == msg_counter) {
//           found_current = true;
//           count++;
//           LOG_INFO("Found ack " << msg_counter << " from current process (count: " << count << ")")
//       }

//       // Check replicas (not only confirmed followers)
//       for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++)
//           if (!found_replicas.at(replica_idx) &&
//               this->replicas.at(replica_idx).get_replica_ack(active_client_idx, active_slot_idx) == msg_counter) {
//               found_replicas.at(replica_idx) = true;
//               count++;
//               LOG_INFO("Found ack " << msg_counter << " from replica " << this->replicas.at(replica_idx).get_id() << " (count: " << count << ")")
//           }
//   }
// }
