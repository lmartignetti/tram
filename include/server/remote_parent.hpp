#ifndef _REMOTE_PARENT_
#define _REMOTE_PARENT_

#include "datatypes.hpp"
#include "tram_server.hpp"

class hm_slot_mask;

// Remote parent group
class tram_server::remote_parent : public tram_process {
  protected:
    const tram_server &server;

    const size_t gid;             // group id
    const std::vector<int> group; // parent group

    // Input buffer
    unsigned char *raw_buffer; // raw allocated parent buffer
    std::vector<hm_slot_mask> buffer;   // slotted parent buffer
    size_t buffer_head;                 // next slot to check
    entry_type next_seq_h;              // next sequence number to read

    size_t last_l_slot_idx; // last LOG slot with parent as source

  public:
    remote_parent(const size_t gid, std::vector<int> group, const tram_server &server);
    ~remote_parent();

    void reg_local_vars(std::vector<local_shared_variable *> &shared);

    ssize_t check_active_slot_hm(entry_type &seq_h);

    inline size_t get_buffer_len() const noexcept { return this->buffer.size(); };
    inline hm_slot_mask &get_hm_slot_at(size_t slot_idx) { return this->buffer.at(slot_idx); };
    inline size_t get_next_seq_h() const noexcept { return this->next_seq_h; }
    inline size_t get_last_l_slot_idx() const noexcept { return this->last_l_slot_idx; }
    inline size_t get_buffer_head() const noexcept { return this->buffer_head; }

    inline void set_next_seq_h(const size_t next_seq_h) noexcept { this->next_seq_h = next_seq_h; }
    inline void set_last_l_slot_idx(const size_t last_l_slot_idx) noexcept { this->last_l_slot_idx = last_l_slot_idx; }
    inline void set_buffer_head(const size_t buffer_head) noexcept { this->buffer_head = buffer_head; }

    std::string to_string() const;
};

#endif /* _REMOTE_PARENT_ */