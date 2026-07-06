#include <deque>
#include <filesystem>
#include <fstream>
#include <iostream>
#include <nlohmann/json.hpp>
#include <stddef.h>
#include <string>
#include <tuple>
#include <vector>

#include "client/tram_client.hpp"
#include "server/tram_server.hpp"
#include "smcluster.hpp"

#include "utils/logging.hpp"
#include "utils/tram_parse_config.hpp"

namespace fs = std::filesystem;

void get_perf_for(smcluster &cluster, const struct tram_config &conf, const struct tram_config_data &conf_data, std::vector<double> &latency_raw,
                  std::vector<double> &latency, std::vector<double> &throughput);

void execute_for(int pid, const fs::path &config_dir);

std::vector<uint> generate_msg(tram_config conf, u_char *value);

std::string get_current_time_str();

int main(int argc, char *argv[]) {
    int pid = atoi(argv[1]);
    const fs::path config_dir = argv[2];

    LOG_INFO("Benchmarking process " << pid << " started at " << get_current_time_str() << " with config dir: " << config_dir.string())

    // Check input config directory
    if (!fs::exists(config_dir) || !fs::is_directory(config_dir))
        throw std::invalid_argument("Path is not a readable directory: " + config_dir.string());

    execute_for(pid, config_dir);

    return 0;
}

void execute_for(int pid, const fs::path &config_dir) {

    // Parse network configuration file
    std::string network_config_filename = config_dir.string() + "/network.json";
    std::ifstream network_config_file(network_config_filename);
    CHECK(network_config_file.is_open(), "Could not open configuration file: " + network_config_filename)
    nlohmann::json network_json;
    network_config_file >> network_json;
    network_config_file.close();

    std::vector<smcluster_network_entry> network;
    for (const auto &n : network_json) {
        network.push_back({n["ip"].get<std::string>(), n["tcp_port"].get<int>(), n["rdma_port"].get<int>(), n["cpu_core"].get<size_t>()});
    }
    LOG_INFO("Network configuration loaded with " << network.size() << " nodes")

    // Parse data configuration file
    std::string data_config_filename = config_dir.string() + "/data.json";
    std::ifstream data_config_file(data_config_filename);
    CHECK(data_config_file.is_open(), "Could not open configuration file: " + data_config_filename)
    nlohmann::json data_json;
    data_config_file >> data_json;
    data_config_file.close();

    struct tram_config_data conf_data;
    conf_data.duration_ms = data_json["duration_ms"].get<size_t>();
    conf_data.num_msgs = data_json["num_msgs"].get<size_t>();
    conf_data.collect_every_ms = data_json["collect_every_ms"].get<size_t>();
    conf_data.max_latency_samples = data_json["max_latency_samples"].get<size_t>();
    LOG_INFO("Data configuration loaded (duration_ms: " << conf_data.duration_ms << ", num_msgs: " << conf_data.num_msgs
                                                        << ", collect_every_ms: " << conf_data.collect_every_ms
                                                        << ", max_latency_samples: " << conf_data.max_latency_samples << ")")

    // Collect tests configuration files
    std::map<std::string, std::string> test_configs; // flag -> filename
    for (const auto &entry : fs::recursive_directory_iterator(config_dir, fs::directory_options::skip_permission_denied)) {
        if (entry.is_regular_file()) {
            std::string filename = entry.path().filename().string();
            if (filename.compare("network.json") != 0 && filename.compare("data.json") != 0 && filename.compare("tests.conf") != 0) {
                std::string flag = filename.substr(std::string("config_").size());
                flag.resize(flag.size() - 5);
                test_configs[flag] = entry.path().string();
            }
        }
    }

    // Create the cluster
    LOG("Node " << pid << " connecting @ " << network.at(pid).ip_address << ":" << network.at(pid).tcp_port << ", " << network.at(pid).rdma_port)
    smcluster cluster(pid, network);
    LOG("Node " << pid << " connected @ " << network.at(pid).ip_address << ":" << network.at(pid).tcp_port << ", " << network.at(pid).rdma_port)

    for (auto test : test_configs) {
        const std::string &flag = test.first;

        LOG("Executing test " << flag)
        tram_config conf;
        parse_config(test.second, conf);

        std::vector<double> latency_raw_samples; // [num_dst][sample]
        std::vector<double> latency_samples;     // [num_dst][sample]
        std::vector<double> throughput_samples;  // [num_dst][sample]

        get_perf_for(cluster, conf, conf_data, latency_raw_samples, latency_samples, throughput_samples);
        cluster.clear_shared_vars();

        size_t num_servers = conf.tree.size() * conf.group_size;
        if (pid >= num_servers) { // client
            log_data(latency_raw_samples, pid, "r" + flag, "r" + flag);
            log_data(latency_samples, pid, "l" + flag, "l" + flag);
            log_data(throughput_samples, pid, "t" + flag, "t" + flag);
        }

        LOG("")
        LOG("General synching... ")
        cluster.synchronize_all();
        LOG("Test " << flag << " done")
    }
}

void get_perf_for(smcluster &cluster, const struct tram_config &conf, const struct tram_config_data &conf_data, std::vector<double> &latency_raw,
                  std::vector<double> &latency, std::vector<double> &throughput) {

    // Define the role of a process based on the position in the config
    size_t num_servers = conf.tree.size() * conf.group_size;
    bool is_server = cluster.get_pid() < num_servers;
    bool is_client = cluster.get_pid() >= num_servers;

    // Collect measures
    latency_raw.clear();
    size_t latency_raw_idx = 0;
    latency.clear();
    throughput.clear();

    LOG("Configuration: (tree: " << vector_to_string(conf.tree) << ", clients: " << conf.num_clients
                                 << ", payload size: " << payload_str(conf.payload_size) << ")")

    if (is_client) {
        tram_client client(cluster, conf);
        unsigned char value[conf.payload_size];

        LOG("Collecting performance...")

        uint64_t delta_end;
        uint64_t msg_count = 0, last_msg_count = 0;
        uint64_t milliseconds_passed = 1000, collect_timer = conf_data.collect_every_ms;
        double latency_avg = 0.0;

        std::deque<std::chrono::_V2::system_clock::time_point> pending_lat;
        for (size_t i = 0; i < conf.cbuf_len; i++) {
            std::vector<uint> dst = generate_msg(conf, value);

            auto start_am_time = std::chrono::high_resolution_clock::now();
            pending_lat.push_back(start_am_time);
            CHECK(client.atomic_multicast(dst, value), "Atomic multicast failed, but there should be space in the send channel")
        }

        auto start_time = std::chrono::high_resolution_clock::now();
        do {
            // Wait for an ack
            client.wait_ack();
            auto end_am_time = std::chrono::high_resolution_clock::now();

            // Collect measures
            auto start_am_time = pending_lat.front();
            auto delta = std::chrono::duration_cast<std::chrono::nanoseconds>(end_am_time - start_am_time).count();
            pending_lat.pop_front();

            msg_count++;
            double current_msg_count = double(msg_count - last_msg_count);
            latency_avg = (1 / current_msg_count) * (double(delta) + (current_msg_count - 1.0) * latency_avg);

            delta_end = std::chrono::duration_cast<std::chrono::milliseconds>(end_am_time - start_time).count();

            while (delta_end >= milliseconds_passed) {
                LOG("Ordered messages after " << milliseconds_passed / 1000 << "s: " << msg_count)
                milliseconds_passed += 1000;
            }

            if (msg_count <= conf_data.max_latency_samples)
                latency_raw.push_back(delta);
            else {
                latency_raw.at(latency_raw_idx) = delta;
                latency_raw_idx = (latency_raw_idx + 1) % conf_data.max_latency_samples;
            }

            if (delta_end >= collect_timer && collect_timer <= conf_data.duration_ms) {
                // If a timer more that one timer expired, catch up
                while (delta_end >= collect_timer + conf_data.collect_every_ms) {
                    latency.push_back(0.0);
                    throughput.push_back(0.0);
                    collect_timer += conf_data.collect_every_ms;
                }

                // Latency avg
                latency.push_back(latency_avg);
                latency_avg = 0.0;

                // Thorughput
                double throughput_avg = 1000.0 * double(current_msg_count) / double(conf_data.collect_every_ms);
                throughput.push_back(throughput_avg);

                collect_timer += conf_data.collect_every_ms;
                last_msg_count = msg_count;
            }

            // If not done, then broadcast again
            if (conf_data.duration_ms != 0 ? delta_end < conf_data.duration_ms : msg_count < conf_data.num_msgs) {
                std::vector<uint> dst = generate_msg(conf, value);

                auto start_am_time = std::chrono::high_resolution_clock::now();
                pending_lat.push_back(start_am_time);
                CHECK(client.atomic_multicast(dst, value), "Atomic multicast failed, but there should be space in the send channel")
            }
        } while (conf_data.duration_ms != 0 ? delta_end < conf_data.duration_ms : msg_count < conf_data.num_msgs);

        // Fill the placeholders with the next latency, which is always there
        for (ssize_t i = latency.size() - 1; i >= 0; i--)
            if (latency.at(i) == 0.0)
                latency.at(i) = latency.at(i + 1);

        LOG("Performance collected (tree: " << vector_to_string(conf.tree) << ", clients: " << conf.num_clients
                                            << ", payload size: " << payload_str(conf.payload_size) << ")")

    } else if (is_server)
        tram_server server(cluster, conf);

    LOG("Cluster synching... ")
    cluster.synchronize_all();
    LOG("Cluster synched")
}

std::vector<uint> generate_msg(tram_config conf, u_char *value) {
    // Generate the value
    for (size_t i = 0; i < conf.payload_size; i += sizeof(int)) {
        int random = std::rand();
        memcpy(value + i, &random, sizeof(int));
    }

    // Generate destinations
    tree tree(conf.tree);
    std::vector<uint> dest_groups;
    std::vector<uint> dest_groups_left;

    for (size_t d = 0; d < conf.tree.size(); d++)
        dest_groups_left.push_back(d);

    do {
        uint dst_idx = rand() % dest_groups_left.size();
        uint dst = dest_groups_left.at(dst_idx);

        // Genuine logic
        if (conf.genuine && dest_groups.size() > 0) {
            bool direct_hop = false;
            for (uint d : dest_groups) {
                if (tree.get_parent(d) == dst) {
                    direct_hop = true;
                    break;
                }
                for (unsigned int child : tree.get_children(d)) {
                    if (child == dst) {
                        direct_hop = true;
                        break;
                    }
                }
            }
            if (!direct_hop)
                continue;
        }

        dest_groups.push_back(dest_groups_left.at(dst_idx));
        dest_groups_left.erase(dest_groups_left.begin() + dst_idx);
    } while (dest_groups.size() < conf.num_destinations);

    return dest_groups;
}

std::string get_current_time_str() {
    auto now = std::chrono::high_resolution_clock::now();
    auto now_ms = std::chrono::duration_cast<std::chrono::milliseconds>(now.time_since_epoch()) % 1000;
    std::time_t now_c = std::chrono::system_clock::to_time_t(now);

    std::stringstream ss;
    ss << std::put_time(std::localtime(&now_c), "%Y-%m-%d %H:%M:%S") << "." << std::setfill('0') << std::setw(3) << now_ms.count();
    return ss.str();
}
