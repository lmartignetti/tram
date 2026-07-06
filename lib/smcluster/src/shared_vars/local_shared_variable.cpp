#include "smcluster.hpp"

#include "logging.hpp"
#include "remote_process/remote_process.hpp"
#include "remote_process/utils.hpp"

local_shared_variable::local_shared_variable(void *address, size_t size, std::vector<remote_process *> remotes)
    : shared_variable(address, size), remotes(remotes) {

    // Compute remote pids and register a memory region for each remote
    for (remote_process *remote : remotes) {
        this->remote_pids.push_back(remote->get_pid());
        this->mr.push_back(remote->register_memory_region((void *)this->address, this->size));
    }
    LOG_INFO("[LOC VAR] Created local variable (size: " << this->size << ", remotes: " << vector_to_string(this->remote_pids) << ")");
}

local_shared_variable::~local_shared_variable() {
    LOG_INFO("Destructor of local_shared_variable: address " << std::hex << (uintptr_t)address << std::dec << ", size " << this->size << ", remotes "
                                                             << vector_to_string(this->remotes))

    LOG_INFO("Destruction of " << this->mr.size() << " memory regions")
    for (ibv_mr *mr_temp : this->mr) {
        if (ibv_dereg_mr(mr_temp) == 0)
            LOG_INFO("Memory region at 0x" << std::hex << (uintptr_t)mr_temp << std::dec << " destroyed")
        else
            LOG_ERROR("Failed to destroy memory region at 0x" << std::hex << (uintptr_t)mr_temp << std::dec << ": " << strerror(errno))
    }
}

bool local_shared_variable::recv(size_t remote_pid) { return this->wait(remote_pid, this->recv_async(remote_pid)); }

uint64_t local_shared_variable::recv_async(size_t remote_pid) {
    // Check if the remote is valid
    bool found = false;
    size_t remote_idx = 0;
    for (; remote_idx < this->remotes.size(); remote_idx++) {
        if (remote_pid == this->remotes.at(remote_idx)->get_pid()) {
            found = true;
            break;
        }
    }
    CHECK(found, "This variable was not shared with remote " + std::to_string(remote_pid))
    remote_process *remote = this->remotes.at(remote_idx);

    // Post a request
    return remote->remote_op_handler(remote_op::RECV, *this, this->mr.at(remote_idx));
}

bool local_shared_variable::send(size_t remote_pid) { return this->wait(remote_pid, this->send_async(remote_pid)); }

uint64_t local_shared_variable::send_async(const size_t remote_pid) {
    // Check if the remote is valid
    bool found = false;
    size_t remote_idx = 0;
    for (; remote_idx < this->remotes.size(); remote_idx++) {
        if (remote_pid == this->remotes.at(remote_idx)->get_pid()) {
            found = true;
            break;
        }
    }
    CHECK(found, "This variable was not shared with remote " + std::to_string(remote_pid))
    remote_process *remote = this->remotes.at(remote_idx);

    // Post a request
    return remote->remote_op_handler(remote_op::SEND, *this, this->mr.at(remote_idx));
}

bool local_shared_variable::wait(const size_t remote_pid, const uint64_t op_id) {
    // Check if the remote is valid
    bool found = false;
    size_t remote_idx = 0;
    for (; remote_idx < this->remotes.size(); remote_idx++) {
        if (remote_pid == this->remotes.at(remote_idx)->get_pid()) {
            found = true;
            break;
        }
    }
    CHECK(found, "This variable was not shared with remote " + std::to_string(remote_pid))
    remote_process *remote = this->remotes.at(remote_idx);

    uint64_t delay = 0;
    return remote->remote_poll_handler(op_id, delay);
}

// TODO rdma_mtx
void local_shared_variable::ch_perm(size_t remote_pid, int access) {
    // Check if the remote is valid
    bool found = false;
    size_t remote_idx = 0;
    for (; remote_idx < this->remotes.size(); remote_idx++) {
        if (remote_pid == this->remotes.at(remote_idx)->get_pid()) {
            found = true;
            break;
        }
    }
    CHECK(found, "This variable was not shared with remote " + std::to_string(remote_pid))
    remote_process *remote = this->remotes.at(remote_idx);

    // remote->rdma_mtx.lock();

    // Change memory region permissions
    CHECK(ibv_rereg_mr(this->mr.at(remote_idx),
                       IBV_REREG_MR_CHANGE_ACCESS,      // Only change access flags
                       this->mr.at(remote_idx)->pd,     // PD remains the same
                       this->mr.at(remote_idx)->addr,   // Buffer remains the same
                       this->mr.at(remote_idx)->length, // Buffer size remains the same
                       access) == 0,                    // New access flags
          "ibv_rereg_mr failed");

    // remote->rdma_mtx.unlock();
}

void local_shared_variable::set_perm_rw(size_t remote) { ch_perm(remote, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE); }

void local_shared_variable::set_perm_r(size_t remote) { ch_perm(remote, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ); }

void local_shared_variable::set_perm_w(size_t remote) { ch_perm(remote, IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_WRITE); }

void local_shared_variable::set_perm_none(size_t remote) { ch_perm(remote, IBV_ACCESS_LOCAL_WRITE); }

std::string local_shared_variable::to_string() { return hex_dump(this->address, this->size); }
