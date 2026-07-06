#ifndef _REMOTE_CHILD_
#define _REMOTE_CHILD_

#include <map>

#include "datatypes.hpp"
#include "tram_server.hpp"

class hm_slot_mask;

// Remote child group
class tram_server::remote_child : public tram_process {
  protected:
    const tram_server &server;

    const unsigned int gid;
    const std::vector<int> group; // child group

    const size_t buffer_len;

    // Input buffer
    ssize_t buffer_head; // next slot to write
    entry_type next_seq; // next sequence number to write on the child H slot

    std::vector<remote_shared_variable *> HM_full;                                  // [process_idx] - full child buffer
    std::vector<std::vector<std::pair<hm_slot_mask, remote_shared_variable *>>> HM; // [process_idx][slot_idx] - slotted child buffer

    std::vector<std::pair<remote_shared_variable *, uint64_t>> pending_ops;

  public:
    remote_child(unsigned int gid, std::vector<int> group, const tram_server &server);
    ~remote_child();

    void store_remote_vars(const std::map<int, std::vector<remote_shared_variable *>> &remote_vars);

    void write_next_hm_slot(hm_slot_mask &active_hm_slot);
    void wait_pending_ops();
    timestamp recover();

    inline unsigned int get_gid() const noexcept { return this->gid; }
    inline const std::vector<int> &get_group_ids() const noexcept { return this->group; }
    inline size_t get_buffer_head() const noexcept { return this->buffer_head; }
    inline size_t get_next_seq() const noexcept { return this->next_seq; }

    std::string to_string() const;
};

#endif /* _REMOTE_CHILD_ */