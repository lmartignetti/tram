#include "smcluster_parse_config.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "logging.hpp"

void parse_config(int pid, std::string config_path, smcluster_config &conf) {
    conf.pid = pid;

    std::ifstream file(config_path);
    CHECK(file.is_open(), "Could not open configuration file: " + config_path)
    nlohmann::json j;
    file >> j;
    file.close();

    LOG_INFO("Configuration:" << std::endl << j.dump(2) << std::endl)

    conf.network_configuration.clear();
    for (const auto &netconf_entry : j["network"])
        conf.network_configuration.push_back({netconf_entry["ip_address"].get<std::string>(), netconf_entry["tcp_port"].get<int>(),
                                              netconf_entry["rdma_port"].get<int>(), netconf_entry["cpu_core"].get<size_t>()});
}