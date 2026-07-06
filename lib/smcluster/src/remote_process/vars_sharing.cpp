#include "remote_process/remote_process.hpp"

#include "logging.hpp"

struct __attribute__((packed)) remote_process::mr_metadata {
    unsigned char *addr;
    size_t length;
    uint32_t key;
};

struct ibv_mr *remote_process::register_memory_region(void *addr, int length) {
    int access = IBV_ACCESS_LOCAL_WRITE | IBV_ACCESS_REMOTE_READ | IBV_ACCESS_REMOTE_WRITE;
    struct ibv_mr *mr = ibv_reg_mr(this->res->pd, addr, length, access);
    CHECK(mr != nullptr, "ibv_reg_mr of " + std::to_string(length) + " bytes failed")
    LOG_INFO("Memory region of size " << length << " registered at 0x" << std::hex << (uintptr_t)mr << " (addr: 0x" << (uintptr_t)addr << ", lkey: 0x"
                                      << mr->lkey << ", rkey: 0x" << mr->rkey << std::dec << ")")
    return mr;
}

// this must be called by both sides in a synchronized way
void remote_process::share_memory(std::vector<local_shared_variable *> vars) {
    int write_bytes;
    int read_bytes;

    // Count the variables to share with the remote process
    size_t local_vars_count = 0;
    for (local_shared_variable *var : vars)
        for (int rem : var->get_remotes())
            if (rem == this->pid)
                local_vars_count++;

    LOG_INFO("Sharing variables with " << this->pid)

    // Check how many variables the remote process wants to share
    size_t remote_vars_count;
    sock_sync_data(sizeof(size_t), &local_vars_count, &remote_vars_count);
    LOG_INFO("Current shared variables count: " << local_vars_count)
    LOG_INFO("Remote shared variables count: " << remote_vars_count)

    unsigned char *local_info = new unsigned char[local_vars_count * sizeof(mr_metadata)];
    unsigned char *remote_info = new unsigned char[remote_vars_count * sizeof(mr_metadata)];

    // Prepare a single big variable with all the metadata
    mr_metadata *local_info_ptr = (mr_metadata *)local_info;
    for (local_shared_variable *local_var : vars) {
        std::vector<int> remote_pids = local_var->get_remotes();
        for (size_t remote_idx = 0; remote_idx < remote_pids.size(); remote_idx++) {
            int remote_pid = remote_pids.at(remote_idx);
            if (remote_pid == this->pid) {
                local_info_ptr->addr = local_var->address;
                local_info_ptr->length = local_var->size;
                local_info_ptr->key = local_var->mr.at(remote_idx)->rkey;
                LOG_INFO("Local var ready (addr: 0x" << std::hex << (uintptr_t)local_info_ptr->addr << std::dec << ", size: "
                                                     << local_info_ptr->length << ", rkey: 0x" << std::hex << local_info_ptr->key << std::dec << ")")
                local_info_ptr++;
            }
        }
    }

    // Send the local shared variables info
    write_all(this->sockfd, local_info, local_vars_count * sizeof(mr_metadata));
    LOG_INFO("Sent local variables info: " << local_vars_count * sizeof(mr_metadata) << " bytes sent")

    // Receive the remote shared variables info
    read_all(this->sockfd, remote_info, remote_vars_count * sizeof(mr_metadata));
    LOG_INFO("Received remote variables info: " << remote_vars_count * sizeof(mr_metadata) << " bytes received")

    // Parse the remote shared variables info
    mr_metadata *remote_info_ptr = (mr_metadata *)remote_info;
    size_t local_rem_qp_num_idx = 0;
    for (int rem = 0; rem < remote_vars_count; rem++) {
        try {
            this->remote_shared_variables.push_back(std::unique_ptr<remote_shared_variable>(
                new remote_shared_variable(remote_info_ptr->addr, remote_info_ptr->length, remote_info_ptr->key, this)));
        } catch (const std::exception &e) {
            delete[] local_info;
            delete[] remote_info;
            throw e;
        }

        remote_info_ptr++;
    }

    // sync to make sure that both sides are in states that they can connect to prevent packet lose
    char local_char = 'B';
    char remote_char;
    sock_sync_data(1, &local_char, &remote_char);
    LOG_INFO("Sharing variables with " << this->pid << " done" << std::endl)

    delete[] local_info;
    delete[] remote_info;
}

void remote_process::clear_remote_variables() {
    // for (size_t var_idx = 0; var_idx < this->remote_shared_variables.size(); var_idx++)
    //     delete this->remote_shared_variables.at(var_idx);
    this->remote_shared_variables.clear();
}
