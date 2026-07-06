#include "smcluster.hpp"

#include <fstream>
#include <netdb.h>
#include <thread>
#include <unistd.h>

#include "logging.hpp"
#include "remote_process/remote_process.hpp"

void pin_current_thread_to_cpu(size_t cpu_id);

smcluster::smcluster(int pid, std::vector<smcluster_network_entry> &network_configuration, int cq_size)
    : pid(pid), network_configuration(network_configuration), cq_size(cq_size + 2) {

    this->sync_count = 0;

    pin_current_thread_to_cpu(this->network_configuration.at(this->pid).cpu_core);

    // Preconditions
    CHECK(this->pid >= 0 && this->pid < this->network_configuration.size(),
          "Bad pid " + std::to_string(this->pid) + " < " + std::to_string(this->network_configuration.size()))
    for (smcluster_network_entry network_configuration_entry : this->network_configuration) {
        CHECK(network_configuration_entry.tcp_port > 0, "Bad tcp port " + std::to_string(network_configuration_entry.tcp_port))
        CHECK(network_configuration_entry.rdma_port > 0, "Bad rdma port " + std::to_string(network_configuration_entry.rdma_port))
    }
    CHECK(!this->network_configuration.empty(), "Network cannot be empty")

    // Print network configuration
    LOG_INFO("Network configuration")
    for (int pid = 0; pid < this->network_configuration.size(); pid++) {
        LOG_INFO("> Node " << pid << " @ " << this->network_configuration.at(pid).ip_address << ":" << this->network_configuration.at(pid).tcp_port)
    }
    LOG_INFO("Creation of current node: pid = " << this->pid)

    // Perform TCP connections
    std::vector<int> sock_fds = this->connect_tcp();

    // Perform RDMA connections
    this->connect_rdma(sock_fds);

    // Check that remotes are correctly ordered
    for (size_t i = 0; i < this->remotes.size(); i++) {
        remote_process *remote = this->remotes.at(i).get();
        if (i == this->pid) {
            // current process
            CHECK(remote == nullptr, "Current process index should be null: found pid " + std::to_string(remote->get_pid()))
            continue;
        }
        CHECK(remote != nullptr, "Remote " + std::to_string(i) + " should not be null")
        CHECK(remote->get_pid() == i, "Remotes are not in order: found " + std::to_string(remote->get_pid()) + " in place of " + std::to_string(i))
    }

    // freeaddrinfo(resolved_addrinfo);

    this->synchronize_all();

    LOG_INFO("All connections are established" << std::endl)
}

local_shared_variable *smcluster::reg_local_var(void *address, size_t size, std::vector<int> remote_pids) {
    // Extract the remote_process pointers from the pids
    std::vector<remote_process *> remotes;
    for (int remote_pid : remote_pids) {
        CHECK(remote_pid != this->pid, "Cannot share a variable with the current process")
        CHECK(remote_pid >= 0 && remote_pid < this->remotes.size(),
              "Remote " << remote_pid << " out of range [0, " << this->remotes.size() - 1 << "]")
        remotes.push_back(this->remotes.at(remote_pid).get());
    }

    // Create and register the local shared variable
    auto l = std::unique_ptr<local_shared_variable>(new local_shared_variable(address, size, remotes));
    local_shared_variable *l_ptr = l.get();
    this->local_shared_variables.push_back(std::move(l));
    LOG_INFO("Registered local variable #" << this->local_shared_variables.size() - 1 << std::endl)

    return l_ptr;
}

void smcluster::share_variables(std::vector<local_shared_variable *> vars) {
    for (std::unique_ptr<remote_process> &remote : this->remotes)
        if (remote != nullptr)
            remote->share_memory(vars);
}

// TODO
void smcluster::clear_shared_vars() {
    this->synchronize_all();

    this->local_shared_variables.clear();

    for (std::unique_ptr<remote_process> &remote : this->remotes) {
        if (remote == nullptr)
            continue;
        remote->clear_remote_variables();
    }

    this->synchronize_all();
}

uint32_t smcluster::get_inline_size(size_t remote) {
    // Check if the remote is valid
    CHECK(remote != this->pid, "It does not make sense to check if the current node is active")
    CHECK(remote < this->remotes.size(),
          "Remote " + std::to_string(remote) + " out of range: only " + std::to_string(this->remotes.size()) + " remotes defined")

    return this->remotes.at(remote)->get_max_inline_size();
}

void smcluster::synchronize_all() {
    size_t remote;

    LOG_INFO("Synchronizing...")

    for (size_t i = 0; i < this->remotes.size(); i++)
        if (i != this->pid) {
            this->remotes.at(i)->sock_sync_data(sizeof(size_t), &this->sync_count, &remote);
            CHECK(remote == this->sync_count,
                  "Synch not matching with remote " << i << "... local: " + std::to_string(this->sync_count) + ", remote: " + std::to_string(remote))
        }

    this->sync_count++;

    LOG_INFO("All processes are synchronized on counter " << this->sync_count << std::endl)
}

std::vector<std::vector<remote_shared_variable *>> smcluster::get_remote_variables(std::vector<size_t> expected_counts) {
    CHECK(expected_counts.size() == 0 || expected_counts.size() == this->remotes.size(), "If specified, expected counts are needed for all "
                                                                                             << this->remotes.size() << " remote, but "
                                                                                             << expected_counts.size() << " were specified")

    std::vector<std::vector<remote_shared_variable *>> v;
    for (size_t i = 0; i < this->remotes.size(); i++) {
        remote_process *remote = this->remotes.at(i).get();

        if (remote == nullptr)
            v.push_back({});
        else
            v.push_back(remote->get_remote_variables());

        if (expected_counts.size() == this->remotes.size())
            CHECK(v.back().size() == expected_counts.at(i), "Shared " << v.back().size() << " variables instead of " << expected_counts.at(i))
    }

    return v;
}

void pin_current_thread_to_cpu(size_t cpu_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(cpu_id, &cpuset);

    pthread_t thread = pthread_self();
    CHECK(pthread_setaffinity_np(thread, sizeof(cpu_set_t), &cpuset) == 0, "Error setting thread affinity")
}
