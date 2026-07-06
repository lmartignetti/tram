#ifndef _SMCLUSTER_
#define _SMCLUSTER_

#include <infiniband/verbs.h>
#include <rdma/rdma_cma.h>

#include <map>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

#include "utils/smcluster_parse_config.hpp"

class local_shared_variable;
class remote_shared_variable;
class remote_process;

class smcluster {
    friend class local_shared_variable;
    friend class remote_shared_variable;

    const int pid;                                              // process identifier
    std::vector<smcluster_network_entry> network_configuration; // network configuration as list of IP address and TCP port and rdma port
    int listen_fd;                                              // listening socket
    size_t sync_count;                                          // counter to sync

    int cq_size; // completion queue size
    struct ibv_context *ctx;
    rdma_event_channel *event_channel;
    struct rdma_cm_id *cm_id_listen;                                            // cm_id to listen for rdma connections
    std::vector<std::unique_ptr<remote_process>> remotes;                       // this will contain a nullptr in the place of the current process
    std::vector<std::unique_ptr<local_shared_variable>> local_shared_variables; // local shared variables

    std::vector<int> connect_tcp();
    void connect_rdma(std::vector<int> sock_fds);

    void accept_remote();
    void connect_remote(int remote_pid);

    void recovery_accept(int remote);

  public:
    /**
     * @brief Construct a new shared memory cluster object. Upon construction, the tcp and rdma connections will be performed. The
     * "network_configuration" must be specified as a list of tuples <IP address, TCP port, RDMA port>, with the index of each item representing the
     * corresponding process identifier.
     *
     * @param pid local process identifier
     * @param network_configuration network configuration
     * @param cq_size completion queue size
     */
    smcluster(int pid, std::vector<smcluster_network_entry> &network_configuration, int cq_size = 10);

    ~smcluster();

    /**
     * @brief Register a local variable to be able to share it.
     *
     * @param address local address
     * @param size variable size
     * @param remotes remotes to share the variable with
     * @return local_shared_variable* - pointer to the registered variable (see "share_variables()")
     */

    local_shared_variable *reg_local_var(void *address, size_t size, std::vector<int> remotes);

    /**
     * @brief Share local variables with the remote processes. A local copy of each remote shared variable is allocated. This method must be called by
     * each process in the cluster in a synchronized way.
     *
     * @param shared_variables registered variables to share
     */
    void share_variables(std::vector<local_shared_variable *> shared_variables);

    /**
     * This method must be called by each process in the cluster in a synchronized way.
     */
    void clear_shared_vars();

    /**
     * @brief Get the remote shared variables handles. The first dimension is the process identifier, the second dimension is the remote variable
     * index in the order in which variables have been shared by the remote. The index corresponding to the current process will always be an empty
     * list.
     *
     * @return std::vector<std::vector<remote_shared_variable *>> - remote shared variables for each remote
     */
    std::vector<std::vector<remote_shared_variable *>> get_remote_variables(std::vector<size_t> expected_counts = {});

    /**
     * @brief Check if a given remote is active. If not active, the remote went to error state and a recovery is necessary (see "recovery()"). A
     * remote can go to error state for different reasons, among which performing a remote operation without the correct permissions.
     *
     * @param remote remote to check
     * @return true - if active;
     * @return false - if in error state
     */
    bool is_remote_active(size_t remote);

    uint32_t get_inline_size(size_t remote);

    /**
     * @brief Perform the recovery of a given remote which is no longer active. This method must be called by the remote in a synchronized way.
     *
     * @param remote remote to recover
     */
    void recovery(size_t remote);

    /**
     * @brief Synchronize all the processes in the cluster through TCP. This method must be called by each process in the cluster in a synchronized
     * way.
     *
     */
    void synchronize_all();

    /**
     * @brief Get the local process identifier.
     *
     * @return int - local process identifier
     */
    int get_pid() const noexcept { return this->pid; }

    /**
     * @brief Get the number of process in the cluster.
     *
     * @return int - number of processes
     */
    int get_num_processes() const noexcept { return this->remotes.size(); }

    int get_cq_size() const noexcept { return this->cq_size; }
};

class shared_variable {
  protected:
    unsigned char *const address; // local or remote address
    const size_t size;            // size

  public:
    shared_variable(void *address, size_t size);
    ~shared_variable();

    unsigned char *get_address() const noexcept { return this->address; }
    size_t get_size() const noexcept { return this->size; }
};

class local_shared_variable : public shared_variable {
    friend class smcluster;
    friend class remote_process;

  private:
    const std::vector<remote_process *> remotes; // list of remotes to share the variable with
    std::vector<int> remote_pids;

    std::vector<ibv_mr *> mr; // a memory region for each remote the local variable is shared with

    local_shared_variable(void *address, size_t size, std::vector<remote_process *> remotes);

    void ch_perm(size_t remote, int access);

  public:
    ~local_shared_variable();

    bool recv(size_t remote);
    uint64_t recv_async(size_t remote);
    bool send(size_t remote);
    uint64_t send_async(const size_t remote_pid);
    bool wait(const size_t remote_pid, const uint64_t op_id);

    void set_perm_rw(size_t remote);
    void set_perm_r(size_t remote);
    void set_perm_w(size_t remote);
    void set_perm_none(size_t remote);

    std::vector<int> get_remotes() const noexcept { return this->remote_pids; };

    std::string to_string();
};

class remote_shared_variable : public shared_variable {
    friend class smcluster;
    friend class remote_process;

  private:
    remote_process *remote; // remote

    unsigned char *const remote_address; // remote address
    const uint32_t remote_key;           // remote key of the memory region

    // TODO: do not allocate a copy: take an address as parameter of the constructor (need to reg mr)
    ibv_mr *mr; // memory region of the local copy

    remote_shared_variable(void *remote_address, size_t size, uint32_t remote_key, remote_process *p);

  public:
    ~remote_shared_variable();

    bool read();
    uint64_t read_async();

    bool write();
    uint64_t write_async();

    bool wait(uint64_t op_id);

    int get_remote() const noexcept;

    std::string to_string();
};

#endif /* _SMCLUSTER_ */