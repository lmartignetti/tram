#include "tram_parse_config.hpp"

#include <fstream>
#include <nlohmann/json.hpp>

#include "utils/logging.hpp"

void parse_config(std::string config_path, tram_config &conf) {
    std::ifstream file(config_path);
    CHECK(file.is_open(), "Could not open configuration file: " + config_path)
    nlohmann::json cluster;
    file >> cluster;
    file.close();

    // LOG_INFO("Parsed configuration:" << std::endl << j.dump(4))

    for (const auto &parent : cluster["tree"])
        conf.tree.push_back(parent.get<int>());
    conf.group_size = cluster["group_size"].get<size_t>();
    conf.num_clients = cluster["num_clients"].get<size_t>();
    conf.cbuf_len = cluster["cbuf_len"].get<size_t>();
    conf.payload_size = cluster["payload_size"].get<size_t>();
    conf.pending_len = cluster["pending_len"].get<size_t>();
    conf.log_len = cluster["log_len"].get<size_t>();
    conf.genuine = cluster["genuine"].get<bool>();
    conf.num_destinations = cluster["num_destinations"].get<size_t>();
}
