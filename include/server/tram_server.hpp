#ifndef _TRAM_SERVER_HPP_
#define _TRAM_SERVER_HPP_

#include <atomic>
#include <mutex>
#include <thread>
#include <vector>

#include "smcluster.hpp"
#include "tram_process.hpp"
#include "tree.hpp"

class h_slot_mask;
class l_slot_mask;
class hm_slot_mask;

class tram_server : public tram_process {
  private:
    class remote_client;
    class remote_parent;
    class remote_replica;
    class remote_child;

    const size_t ping_delta = 500; // ms
    size_t server_buffer_size;

    // Consensus variables
    entry_type rnd;                               // round
    entry_type FUO;                               // First Undecided Offset
     unsigned char *LOG;                  // LOG
    std::vector<l_slot_mask> LOG_slots;           // [slot_idx] - LOG slots
    std::atomic_int leader;                       // leader of the current group
    std::vector<size_t> confirmed_followers_idxs; // indexes of confirmed followers in replicas vector

    entry_type next_seq_l; // server sequence number

    size_t last_served_client_idx; // last served client

    // Failover
    std::chrono::system_clock::time_point last_ping_time;
    ssize_t last_seq_l_ping; // as leader, ping if the difference with the current one is too small

    // Utility variables
    std::atomic_bool running_server;
    std::thread server_thread;
    bool locked;
    std::mutex lead_mutex;
    std::atomic_bool running_recovery_thread;
    std::thread recovery_thread;

    // Remote processes
    std::vector<remote_client> clients;    // client processes
    std::unique_ptr<remote_parent> parent; // parent group
    std::vector<remote_replica> replicas;  // replica processes
    std::vector<remote_child> children;    // child groups

    // Redundant variables
    unsigned int gid;
    int parent_group; // Parent group id

  private:
    void server_thr();

    // Consensus
    bool task1(hm_slot_mask &active_hm_slot, entry_type source, entry_type active_client_idx);
    // Forward to children
    void task2(hm_slot_mask &active_hm_slot);
    // Ack to client
    void task3(hm_slot_mask &active_hm_slot, entry_type active_client_idx);

    void recovery_thr(std::atomic_bool &ready);
    void recover_remote(remote_replica *replica, std::atomic_bool *done);
    void followers_ping();

    /**
     * @brief Wait for the value acks from the followers plus the current process (leader). Since the value is written after the header in a single HM
     * write, once I read the header, I am sure that the value is there in the current process.
     *
     * @param client_idx
     * @param active_slot_idx
     */
    // void wait_value_acks(h_slot_mask &active_slot, entry_type client_idx);

    // size_t find_active_m_slot(entry_type &active_client_idx, entry_type &seq_m);

    // Utility functions

    /**
     * @brief Find an active slot to be processed
     *
     * @param source output - source of the slot: either num_clients (if parent) or client_idx
     * @return size_t - slot index
     */
    size_t find_active_slot(entry_type &source);

    ssize_t follower();
    ssize_t check_perm_requests();
    // Every now and then, perform a remote operation to check if replicas are alive
    void replicas_ping();

    // Revoke old leader permissions and gather permissions from a quorum of replicas
    bool failover_task1();
    // Leader catchup (recover rnd, FUO and LOG)
    bool failover_task2();
    // Find the last seq for each input source (parent + clients)
    void failover_task3();
    // Find the last client ack for each client
    void failover_task4();
    // Find the last slot idx and seq for each child
    std::vector<timestamp> failover_task5();
    // Manage the last log entry: resend client ack (if needed) and rewrite on children
    void failover_task6(std::vector<timestamp> last_msg_ids);
    // void failover_task7(size_t g_idx, size_t last_l_slot_idx, size_t recent_p_idx);

  public:
    tram_server(smcluster &cluster, const tram_config conf);

    ~tram_server();

    bool lead();

    inline bool is_leader() const noexcept { return this->leader == this->cluster.get_pid(); }

    std::string to_string();
};

#endif /* _TRAM_SERVER_HPP_ */