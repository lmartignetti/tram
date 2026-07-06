#ifndef _SMCLUSTER_PARSE_CONFIG_
#define _SMCLUSTER_PARSE_CONFIG_

#include <stddef.h>
#include <string>
#include <vector>

struct smcluster_network_entry {
    const std::string ip_address;
    const int tcp_port;
    const int rdma_port;
    const size_t cpu_core;

    smcluster_network_entry(const std::string &ip, int tcp, int rdma, size_t cpu) : ip_address(ip), tcp_port(tcp), rdma_port(rdma), cpu_core(cpu) {}
};

struct smcluster_config {
    size_t pid;
    std::vector<smcluster_network_entry> network_configuration;
};

void parse_config(int pid, std::string config_path, smcluster_config &conf);

#endif /* _SMCLUSTER_PARSE_CONFIG_ */
