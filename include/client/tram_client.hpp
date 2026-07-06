#ifndef _TRAM_CLIENT_HPP_
#define _TRAM_CLIENT_HPP_

#include <queue>
#include <vector>

#include "datatypes.hpp"
#include "smcluster.hpp"
#include "tram_process.hpp"
#include "tree.hpp"

class v_slot_mask;
class m_slot_mask;
class h_slot_mask;
class hm_slot_mask;

class tram_client : public tram_process {
  private:
    timestamp lc; // client logical clock <pid, seq>

    std::vector<entry_type> next_seq_h; // next seq for each group
    std::vector<entry_type> ack;        // last ack seen from each group
    std::queue<size_t> pending_idxs;    // pending messages
    size_t vptr;

    // Shared variables
    std::vector<v_slot_mask> V;

    std::vector<std::vector<std::pair<hm_slot_mask, remote_shared_variable *>>> HM; // [server_idx][slot] - h_entry + m_entry

  public:
    tram_client(smcluster &cluster, const tram_config conf);

    ~tram_client();

    bool atomic_multicast(std::vector<uint> dst, const void *value);

    void wait_ack();
};

#endif /* _TRAM_CLIENT_HPP_ */