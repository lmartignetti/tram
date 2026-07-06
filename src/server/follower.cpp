#include "server/tram_server.hpp"

#include "logging.hpp"
#include "server/remote_client.hpp"
#include "server/remote_replica.hpp"

ssize_t tram_server::check_perm_requests() {
    for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++) {
        remote_replica &replica = this->replicas.at(replica_idx);

        if (replica.get_suspected())
            continue;

        entry_type replica_perm = replica.get_perm();
        if (replica_perm != 0) {
            // replica.reset_perm();
            int new_leader = replica.get_id();
            LOG_INFO("Found permissions request " << replica_perm << " from replica: " << new_leader)

            // Revoke permissions to the current leader
            if (this->leader != -1 && this->leader != this->cluster.get_pid()) {
                LOG_INFO("Revoking permissions to the old leader: " << this->leader)
                for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++)
                    if (this->replicas.at(replica_idx).get_id() == this->leader)
                        // TODO: fix
                        // this->replicas.at(replica_idx).revoke_permissions();
                        ;
            }

            // Grant permissions to the new leader
            LOG_INFO("Granting permissions to new leader: " << new_leader);
            // TODO: fix
            // replica.grant_permissions();
            if (!replica.write_perm_ack(replica_perm)) {
                LOG_INFO("Could not ack permission request at replica " << new_leader)
                // TODO: fix
                // replica.set_perm();
                replica.set_suspected(true);
                continue;
            }
            this->leader = new_leader;
            LOG_INFO("Current follower replied with " << replica_perm << " to permission request: leader is now " << this->leader << std::endl)

            return replica_idx; // idx in this->group_servers
        }
    }

    return -1;
}

ssize_t tram_server::follower() {
    // Ping replicas if time has come
    auto current_time = std::chrono::high_resolution_clock::now();
    uint64_t delta = std::chrono::duration_cast<std::chrono::milliseconds>(current_time - this->last_ping_time).count();

    if (delta > this->ping_delta) {
        this->replicas_ping();
        this->last_ping_time = std::chrono::high_resolution_clock::now();
    }

    // Check for permission requests
    return this->check_perm_requests();
}

void tram_server::replicas_ping() {
    for (size_t replica_idx = 0; replica_idx < this->replicas.size(); replica_idx++) {
        remote_replica &replica = this->replicas.at(replica_idx);
        if (!replica.ping()) {
            LOG_WARN("Ping failed for replica " << replica.get_id())
            replica.set_suspected(true);
        }
    }
}

// size_t tram_server::find_active_m_slot(entry_type &active_client_idx, entry_type &seq_m) {
//     // Check the value in the client buffers
//     for (; this->last_served_client_idx < this->clients.size(); this->last_served_client_idx++) {
//         remote_client &client = this->clients.at(this->last_served_client_idx);
//         ssize_t active_slot_idx = client.check_active_slot_m(seq_m);
//         if (active_slot_idx != -1) {
//             active_client_idx = this->last_served_client_idx;
//             this->last_served_client_idx++;
//             return active_slot_idx;
//         }
//     }

//     this->last_served_client_idx = 0;
//     return -1;
// }
