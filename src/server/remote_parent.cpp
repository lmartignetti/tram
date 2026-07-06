#include "server/remote_parent.hpp"

#include <iostream>

#include "logging.hpp"
#include "mem_v.hpp"
#include "server/remote_client.hpp"
#include "structures/hm_slot_mask.hpp"

tram_server::remote_parent::remote_parent(const size_t gid, std::vector<int> group, const tram_server &server)
    : tram_process(server), server(server), gid(gid), group(group) {

    size_t buffer_len = this->server.clients.size() * this->conf.pending_len; // number of slots in the parent buffer
    size_t hm_slot_size =
        hm_slot_mask::get_size(this->server.groups.size(), this->server.alignment, this->conf.payload_size); // size of a single slot
    this->raw_buffer = new (std::align_val_t(alignment))  unsigned char[buffer_len * hm_slot_size]();
    for (size_t slot_idx = 0; slot_idx < buffer_len; slot_idx++) {
         unsigned char *hm_slot_addr = this->raw_buffer + slot_idx * hm_slot_size;
        this->buffer.emplace_back(hm_slot_addr, this->server.groups.size(), this->server.alignment, this->conf.payload_size);
    }
    this->buffer_head = 0;
    this->next_seq_h = 1;
    this->last_l_slot_idx = this->server.conf.log_len; // Initial value is outside the LOG
}

tram_server::remote_parent::~remote_parent() { ::operator delete[]((unsigned char *)this->raw_buffer, std::align_val_t(this->server.alignment)); }

void tram_server::remote_parent::reg_local_vars(std::vector<local_shared_variable *> &shared) {
    shared.push_back(cluster.reg_local_var(
        this->raw_buffer, this->buffer.size() * hm_slot_mask::get_size(this->server.groups.size(), this->server.alignment, this->conf.payload_size),
        this->group));
    for (size_t slot_idx = 0; slot_idx < this->buffer.size(); slot_idx++) {
        hm_slot_mask &hm_slot = this->buffer.at(slot_idx);
        shared.push_back(cluster.reg_local_var(hm_slot.get_m_slot().get_address(), hm_slot.get_size(), this->group));
    }
}

ssize_t tram_server::remote_parent::check_active_slot_hm(entry_type &seq_h) {
    hm_slot_mask &hm_slot = this->buffer.at(this->buffer_head);
    entry_type seq = hm_slot.get_h_slot().get_seq();
    if (seq == this->next_seq_h) {
        LOG_INFO("Processing parent slot (seq: " << std::hex << seq << std::dec << ") at index " << this->buffer_head << ": " << hm_slot.to_string())
        seq_h = this->next_seq_h++;
        size_t active_slot_idx = this->buffer_head;
        this->buffer_head = (this->buffer_head + 1) % this->buffer.size();
        return active_slot_idx;
    }

    return -1;
}

std::string tram_server::remote_parent::to_string() const {
    std::stringstream ss;
    ss << "Remote parent group " << this->gid << " state" << std::endl;
    ss << "- buffer_head: " << this->buffer_head << std::endl;
    ss << "- next_seq_h: " << this->next_seq_h << std::endl;
    ss << "- last_l_slot_idx: " << this->last_l_slot_idx << std::endl;
    ss << "Buffer" << std::endl;
    for (size_t slot_idx = 0; slot_idx < this->buffer.size(); slot_idx++)
        ss << "Slot " << slot_idx << ": " << this->buffer.at(slot_idx).to_string() << std::endl;

    return ss.str();
}
