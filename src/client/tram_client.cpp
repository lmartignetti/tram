#include "client/tram_client.hpp"

#include <cmath>
#include <iostream>

#include "logging.hpp"
#include "mem_v.hpp"
#include "structures/h_slot_mask.hpp"
#include "structures/hm_slot_mask.hpp"
#include "structures/m_slot_mask.hpp"
#include "structures/v_slot_mask.hpp"

// overlay tree as sequence of parents
tram_client::tram_client(smcluster &cluster, const tram_config conf) : tram_process(cluster, conf) {

#ifdef DEBUG
    this->overlay_tree.print_tree();
#endif

    this->vptr = 0;
    this->lc = {(entry_type)this->cluster.get_pid(), 0};

    for (unsigned int gid = 0; gid < this->groups.size(); gid++) {
        this->next_seq_h.push_back(0);
        this->ack.push_back(0);
    }

    for (std::vector<int> g : this->groups)
        for (int p : g)
            CHECK(p != this->cluster.get_pid(), "A client cannot be part of a group")

    LOG_INFO("Destination field size: " << this->dst_field_size)
    LOG_INFO("H entry size: " << h_slot_mask::get_size(this->groups.size(), this->alignment))

    size_t v_size = v_slot_mask::get_size(this->groups.size(), this->alignment);
    for (size_t i = 0; i < this->conf.pending_len; i++)
        this->V.emplace_back(v_slot_mask(new  unsigned char[v_size](), this->groups.size(), this->alignment));

    // Share the variables
    std::vector<local_shared_variable *> shared;
    for (unsigned int gid = 0; gid < this->groups.size(); gid++)
        for (size_t i = 0; i < this->conf.pending_len; i++)
            shared.push_back(cluster.reg_local_var(this->V.at(i).get_ack_address_at(gid), sizeof(entry_type), this->groups.at(gid)));

    this->cluster.share_variables(shared);
    std::vector<std::vector<remote_shared_variable *>> remote_variables = this->cluster.get_remote_variables();

    for (size_t pid = 0; pid < this->cluster.get_num_processes(); pid++)
        this->HM.push_back({});

    for (unsigned int gid = 0; gid < this->groups.size(); gid++) {
        for (int pid : this->groups.at(gid)) {
            LOG_INFO("Process " << pid << " shared " << remote_variables.at(pid).size() << " variables")

            for (size_t slot_idx = 0; slot_idx < this->conf.pending_len; slot_idx++)
                this->HM.at(pid).emplace_back(std::piecewise_construct,
                                              std::forward_as_tuple(remote_variables.at(pid).at(slot_idx)->get_address(), this->groups.size(),
                                                                    this->alignment, this->conf.payload_size),
                                              std::forward_as_tuple(remote_variables.at(pid).at(slot_idx)));
        }
    }

    LOG_INFO("Synchronizing...")
    this->cluster.synchronize_all();

    LOG_INFO("Client constructor done")
}

tram_client::~tram_client() {
    // Wait for all the needed acks
    LOG_INFO("Client done is waiting for remaining acks")
#ifdef DEBUG
    auto start_stale_search = std::chrono::high_resolution_clock::now();
    bool timeout_passed = false;
    uint64_t timeout = 1; // s
#endif
    bool all_ack_found = false;
    while (!all_ack_found) {
#ifdef DEBUG
        auto current_stale_search = std::chrono::high_resolution_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::seconds>(current_stale_search - start_stale_search).count();
        if (!timeout_passed && delta > timeout) {
            timeout_passed = true;
            LOG_INFO("Spent " << timeout << "s waiting for a stale slot... Current V state is:")
            for (size_t slot_idx = 0; slot_idx < this->V.size(); slot_idx++) {
                v_slot_mask &v_slot = this->V.at(slot_idx);
                LOG_INFO("Slot " << slot_idx << ": " << v_slot.to_string())
            }
        }
#endif
        all_ack_found = true;
        for (v_slot_mask &v_slot : this->V) {
            bool entry_ack_found = true;
            for (unsigned int g = 0; g < this->groups.size(); g++) {
                if (v_slot.is_dst(g)) {
                    entry_type ack = v_slot.get_ack_at(g);
                    if (ack == 0) {
                        entry_ack_found = false;
                        break;
                    }
                }
            }
            if (!entry_ack_found) {
                all_ack_found = false;
                break;
            }
        }
    }
    LOG_INFO("All ack found for the atomically multicast messages")

    this->cluster.synchronize_all();

    for (size_t slot_idx = 0; slot_idx < this->conf.pending_len; slot_idx++)
        delete[] this->V.at(slot_idx).get_address();

    LOG_INFO("Tram client destroyed")
}

bool tram_client::atomic_multicast(std::vector<uint> dst, const void *value) {
    LOG_INFO("Atomic multicast of a message to " << vector_to_string(dst))

    bool stale = false;
#ifdef DEBUG
    auto start_stale_search = std::chrono::high_resolution_clock::now();
    bool timeout_passed = false;
    uint64_t timeout = 1; // s
#endif
    while (!stale) {
#ifdef DEBUG
        auto current_stale_search = std::chrono::high_resolution_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::seconds>(current_stale_search - start_stale_search).count();
        if (!timeout_passed && delta > timeout) {
            timeout_passed = true;
            LOG_INFO("Spent " << timeout << "s waiting for a stale slot... Current V state is:")
            for (size_t slot_idx = 0; slot_idx < this->V.size(); slot_idx++) {
                v_slot_mask &v_slot = this->V.at(slot_idx);
                LOG_INFO("Slot " << slot_idx << ": " << v_slot.to_string())
            }
        }
#endif

        stale = true;
        this->vptr = (this->vptr + 1) % this->conf.pending_len;
        v_slot_mask v_slot = this->V.at(this->vptr);
        for (unsigned int g = 0; g < this->groups.size(); g++) {
            if (v_slot.is_dst(g)) {
                entry_type ack = v_slot.get_ack_at(g);
                if (ack == this->ack.at(g) || ack == 0)
                    stale = false;
                else if (ack > this->ack.at(g)) {
                    LOG_INFO("Found ack at slot " << this->vptr << " from group " << g << ": 0x" << std::hex << ack << std::dec)
                    this->ack.at(g) = ack;
                    stale = false;
                }
            }
        }
    }

    v_slot_mask &stale_v_slot = this->V.at(this->vptr);
    LOG_INFO("Found stale V slot " << this->vptr << ": " << stale_v_slot.to_string())

    stale_v_slot.reset();
    stale_v_slot.set_dst(dst);
    LOG_INFO("New V slot " << this->vptr << ": " << stale_v_slot.to_string())

    // Compute lowest common ancestor
    this->lc.counter++;
    int lca = this->overlay_tree.lowest_common_ancestor(dst);
    LOG_INFO("Lowest common ancestor of " << vector_to_string(dst) << " computed to be " << lca)
    this->next_seq_h[lca]++;

    std::vector<std::pair<remote_shared_variable *, uint64_t>> pending_HM_ops;

    // Write the message in the lca processes
    size_t done = 0;
    for (unsigned int s : this->groups.at(lca)) {
        std::pair<hm_slot_mask, remote_shared_variable *> &hm_slot = this->HM.at(s).at(this->vptr);
        hm_slot.first.get_h_slot().set_msg_id(this->lc);
        hm_slot.first.get_h_slot().set_dst(dst);
        hm_slot.first.get_h_slot().set_vptr(this->vptr);
        hm_slot.first.get_h_slot().set_seq(this->next_seq_h.at(lca));
        hm_slot.first.get_m_slot().set_value((const  unsigned char *)value);

        uint64_t wr_id = hm_slot.second->write_async();
        pending_HM_ops.push_back({hm_slot.second, wr_id});
        done++;

        LOG_INFO("Written HM entry to process (" << lca << ", " << s << ") at slot " << this->vptr << ": " << hm_slot.first.to_string())

        // if (done > this->groups.at(lca).size() / 2)
        //     break;
    }
    CHECK(done > this->groups.at(lca).size() / 2,
          "Too many servers failed: " + std::to_string(this->groups.at(lca).size() - done) + " out of " + std::to_string(this->groups.at(lca).size()))

    for (std::pair<remote_shared_variable *, uint64_t> &pending : pending_HM_ops)
        if (!pending.first->wait(pending.second)) {
            LOG_WARN("Operation " << pending.second << "failed" << std::endl)
        }

    LOG_INFO("Multicast done" << std::endl)
    this->pending_idxs.push(this->vptr);
    return true;
}

void tram_client::wait_ack() {
#ifdef DEBUG
    auto start_stale_search = std::chrono::high_resolution_clock::now();
    bool timeout_passed = false;
    uint64_t timeout = 1; // s
#endif
    size_t v_idx = this->pending_idxs.front();
    v_slot_mask &slot = this->V.at(v_idx);
    bool found = false;
    while (!found) {
#ifdef DEBUG
        auto current_stale_search = std::chrono::high_resolution_clock::now();
        auto delta = std::chrono::duration_cast<std::chrono::seconds>(current_stale_search - start_stale_search).count();
        if (!timeout_passed && delta > timeout) {
            timeout_passed = true;
            LOG_INFO("Spent " << timeout << "s waiting for a stale slot... Current V state is:")
            for (size_t slot_idx = 0; slot_idx < this->V.size(); slot_idx++) {
                v_slot_mask &v_slot = this->V.at(slot_idx);
                LOG_INFO("Slot " << slot_idx << ": " << v_slot.to_string())
            }
        }
#endif
        found = true;
        for (unsigned int g = 0; g < this->groups.size(); g++) {
            if (slot.is_dst(g)) {
                entry_type ack = slot.get_ack_at(g);
                if (ack == 0) {
                    found = false;
                    break;
                }
            }
        }
    }
    LOG_INFO("Ack found at V slot " << v_idx)
    this->pending_idxs.pop();
}
