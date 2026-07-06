#ifndef _REMOTE_CLIENT_
#define _REMOTE_CLIENT_

#include "datatypes.hpp"
#include "structures/hm_slot_mask.hpp"
#include "tram_server.hpp"

class tram_server::remote_client : public tram_process {
  protected:
    const tram_server &server;

    const int id; // client id

    std::vector<hm_slot_mask> buffer;
    entry_type next_seq_h; // next sequence number to read

    ssize_t last_l_slot_idx; // last written LOG slot index in which this client is source

    entry_type next_ack; // next ack to write on the client

    std::vector<remote_shared_variable *> r_acks; // remote ack

  public:
    remote_client(int id, const tram_server &server);
    ~remote_client();

    void reg_local_vars(std::vector<local_shared_variable *> &shared);
    void store_remote_vars(std::vector<remote_shared_variable *> remote_vars);

    ssize_t check_active_hm_slot(entry_type &seq_h);

    entry_type read_ack(size_t slot_idx);
    void write_ack(size_t slot_idx);

    inline int get_id() const noexcept { return this->id; }
    inline entry_type get_next_seq_h() const noexcept { return this->next_seq_h; };
    inline hm_slot_mask &get_hm_slot_at(size_t slot_idx) { return this->buffer.at(slot_idx); };
    inline ssize_t get_last_l_slot_idx() const noexcept { return this->last_l_slot_idx; };
    inline entry_type get_next_ack() const noexcept { return this->next_ack; };

    inline void inc_next_seq_h() noexcept { this->next_seq_h++; }
    inline void set_next_seq_h(entry_type last_seq_h) noexcept { this->next_seq_h = last_seq_h; }
    inline void set_last_l_slot_idx(ssize_t last_l_slot_idx) noexcept { this->last_l_slot_idx = last_l_slot_idx; }
    inline void set_next_ack(ssize_t next_ack) noexcept { this->next_ack = next_ack; }

    std::string to_string() const;
};

#endif /* _REMOTE_CLIENT_ */