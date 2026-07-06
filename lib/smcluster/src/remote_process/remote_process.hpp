#include "smcluster.hpp"

#include "utils.hpp"
#include "utils/smcluster_parse_config.hpp"

struct rdma_remote_resources {
    ibv_pd *pd;                        // protection domain
    ibv_cq *cq;                        // completion queue
    ibv_qp *qp;                        // queue pair
    rdma_cm_id *cm_id;                 // rdma communication identifier
    rdma_event_channel *event_channel; // rdma event channel
    size_t max_inline_data;            // maximum inline data size for the queue pair
};

class remote_process {
    friend class smcluster;
    friend class remote_shared_variable;
    struct __attribute__((packed)) mr_metadata;

    const int pid;                                     // remote process identifier
    smcluster &cluster;                                // cluster
    const int sockfd;                                  // socket associated to this remote
    std::shared_ptr<struct rdma_remote_resources> res; // rdma resources associated to this remote

    std::vector<std::unique_ptr<remote_shared_variable>> remote_shared_variables;      // remote shared variables
    uint64_t next_work_request_id;                                                     // to track issued work requests
    std::map<uint64_t, std::tuple<bool, ibv_wc_opcode, ibv_wc_status>> pending_wr_ids; // pending work request ids -> (found, status)

    std::mutex rdma_mtx; // mutual exclusion on operations, permissions changes and recovery

    // Remote operations
    bool poll_completion_event(uint64_t wr_id, uint64_t &delay);
    void remote_op(enum remote_op op_type, shared_variable &shared_var, ibv_mr *mr, size_t op_id);

  public:
    remote_process(int pid, smcluster &cluster, int sockfd, const std::shared_ptr<struct rdma_remote_resources> res);

    // Variable sharing
    struct ibv_mr *register_memory_region(void *addr, int length);
    void share_memory(std::vector<local_shared_variable *> current_shared_variables);
    void clear_remote_variables();

    // Communication methods
    void sock_sync_data(int xfer_size, const void *local_data, void *remote_data);
    static void read_all(int socket_fd, void *buffer, size_t length);
    static void write_all(int socket_fd, const void *buffer, size_t length);

    // Remote operations
    uint64_t remote_op_handler(enum remote_op op_type, shared_variable &shared_var, ibv_mr *mr);
    bool remote_poll_handler(uint64_t wr_id, uint64_t &delay);

    // Recovery
    bool is_active();
    static rdma_cm_id *handle_cm_event(rdma_event_channel *channel, rdma_cm_event *event, enum rdma_cm_event_type type);
    void recovery_connect(int local_pid, const smcluster_network_entry network_entry);

    // Utility
    std::vector<remote_shared_variable *> get_remote_variables();
    int get_pid() const noexcept { return this->pid; }
    uint32_t get_max_inline_size() const noexcept { return this->res->max_inline_data; }
};
