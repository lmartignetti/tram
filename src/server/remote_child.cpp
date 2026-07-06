#include "server/remote_child.hpp"

#include <iostream>

#include "logging.hpp"
#include "mem_v.hpp"
#include "server/remote_client.hpp"
#include "structures/hm_slot_mask.hpp"

tram_server::remote_child::remote_child(unsigned int gid, std::vector<int> group, const tram_server &server)
    : tram_process(server), server(server), gid(gid), group(group), buffer_len(server.clients.size() * server.conf.pending_len) {
    this->buffer_head = 0;
    this->next_seq = 1;
}

tram_server::remote_child::~remote_child() {}

void tram_server::remote_child::store_remote_vars(const std::map<int, std::vector<remote_shared_variable *>> &remote_vars) {
    size_t buffer_len = this->server.clients.size() * this->server.conf.pending_len;

    size_t m_size = m_slot_mask::get_size(this->conf.payload_size);

    for (int pid : this->group) {
        this->HM_full.push_back((remote_vars.at(pid).at(0)));

        this->HM.push_back({});
        for (size_t slot = 0; slot < buffer_len; slot++) {
            this->HM.back().emplace_back(
                std::piecewise_construct,
                std::forward_as_tuple(remote_vars.at(pid).at(1 + slot)->get_address(), this->groups.size(), this->alignment, this->conf.payload_size),
                std::forward_as_tuple(remote_vars.at(pid).at(1 + slot)));
        }
    }
}

void tram_server::remote_child::write_next_hm_slot(hm_slot_mask &active_hm_slot) {
    for (size_t process_idx = 0; process_idx < this->group.size(); process_idx++) {
        unsigned int pid = this->group.at(process_idx);
        std::pair<hm_slot_mask, remote_shared_variable *> &child_slot = this->HM.at(process_idx).at(this->buffer_head);
        child_slot.first.get_h_slot().set(active_hm_slot.get_h_slot());
        child_slot.first.get_h_slot().set_seq(this->next_seq);
        child_slot.first.get_m_slot().set(active_hm_slot.get_m_slot());

        uint64_t wr_id = child_slot.second->write_async();
        pending_ops.push_back({child_slot.second, wr_id});

        LOG_INFO("Written slot to child (" << this->gid << ", " << pid << ") at index " << this->buffer_head << " (op " << wr_id
                                           << "): " << child_slot.first.to_string())
    }

    this->buffer_head = (this->buffer_head + 1) % this->buffer_len;
    this->next_seq++;
}

void tram_server::remote_child::wait_pending_ops() {
    for (std::pair<remote_shared_variable *, uint64_t> &pending_op : this->pending_ops)
        CHECK(pending_op.first->wait(pending_op.second), "Write to some child failed: wr_id " << pending_op.second)
    this->pending_ops.clear();
}

timestamp tram_server::remote_child::recover() {
    size_t hm_slot_size = hm_slot_mask::get_size(this->groups.size(), this->alignment, this->conf.payload_size);
    size_t m_slot_size = m_slot_mask::get_size(this->conf.payload_size);

    // Read all buffers
    for (size_t process_idx = 0; process_idx < this->group.size(); process_idx++) {
        unsigned int pid = this->group.at(process_idx);
        remote_shared_variable *child_process_buffer = this->HM_full.at(process_idx);
        CHECK(child_process_buffer->read(), "Read of buffer failed for child process " + std::to_string(pid));
    }

    // Find the process with the max seq - recover next_seq and buffer_head
    size_t process_idx_max_seq = 0;
    timestamp msg_id_max_seq = {0, 0};
    for (size_t process_idx = 0; process_idx < this->group.size(); process_idx++) {
         unsigned char *child_process_buffer_addr = this->HM_full.at(process_idx)->get_address();
        for (size_t slot_idx = 0; slot_idx < this->buffer_len; slot_idx++) {
             unsigned char *slot_addr = child_process_buffer_addr + slot_idx * hm_slot_size;
            hm_slot_mask hm_slot(slot_addr, this->groups.size(), this->alignment, this->conf.payload_size);
            entry_type hm_slot_seq = hm_slot.get_h_slot().get_seq();

            if (hm_slot_seq >= this->next_seq - 1) {
                this->next_seq = hm_slot_seq + 1;
                this->buffer_head = (slot_idx + 1) % this->buffer_len;
                process_idx_max_seq = process_idx;
                msg_id_max_seq = hm_slot.get_h_slot().get_msg_id();
            }
        }
    }

    // Overwrite the child buffers with the one with max seq
    remote_shared_variable *HM_full_max_seq = this->HM_full.at(process_idx_max_seq);
    for (size_t process_idx = 0; process_idx < this->group.size(); process_idx++) {
        unsigned int pid = this->group.at(process_idx);
        remote_shared_variable *child_process_buffer = this->HM_full.at(process_idx);
        memcpy_v(child_process_buffer->get_address(), HM_full_max_seq->get_address(), HM_full_max_seq->get_size());
        CHECK(child_process_buffer->write(), "Write of buffer failed for child process " + std::to_string(pid));
    }

    // Log
    LOG_INFO("Child " << this->gid << " buffer")
     unsigned char *child_process_buffer_addr = this->HM_full.at(process_idx_max_seq)->get_address();
    for (size_t slot_idx = 0; slot_idx < this->buffer_len; slot_idx++) {
         unsigned char *slot_addr = child_process_buffer_addr + slot_idx * hm_slot_size;
        hm_slot_mask hm_slot(slot_addr, this->groups.size(), this->alignment, this->conf.payload_size);
        LOG_INFO("Slot " << slot_idx << ": " << hm_slot.to_string())
    }

    // Return the msg id of the last written slot
    return msg_id_max_seq;
}

std::string tram_server::remote_child::to_string() const {
    std::stringstream ss;
    ss << "Remote child group " << this->gid << " state" << std::endl;
    ss << "- buffer_head: " << this->buffer_head << std::endl;
    ss << "- next_seq: " << this->next_seq << std::endl;
    return ss.str();
}
