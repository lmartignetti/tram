#ifndef _TRAM_PARSE_CONFIG_
#define _TRAM_PARSE_CONFIG_

#include <stddef.h>
#include <string>
#include <tuple>
#include <vector>

#include "utils/smcluster_parse_config.hpp"

struct tram_config {
    std::vector<int> tree;
    size_t group_size;
    size_t num_clients;
    size_t cbuf_len;
    size_t payload_size;
    size_t pending_len;
    size_t log_len;
    bool genuine;
    size_t num_destinations;
};

struct tram_config_data {
    size_t duration_ms;
    size_t num_msgs; // ignored if durations_ms is specified
    size_t collect_every_ms;
    size_t max_latency_samples;
};

void parse_config(std::string config_path, tram_config &conf);

#endif /* _TRAM_PARSE_CONFIG_ */
