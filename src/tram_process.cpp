#include "tram_process.hpp"

#include "logging.hpp"

std::vector<std::vector<int>> generate_groups(const tram_config conf) {
    std::vector<std::vector<int>> groups;
    for (size_t i = 0; i < conf.tree.size(); i++) {
        groups.push_back({});
        for (size_t j = 0; j < conf.group_size; j++)
            groups.back().push_back(conf.group_size * i + j);
    }
    return groups;
}

tram_process::tram_process(smcluster &cluster, const tram_config conf)
    : cluster(cluster), conf(conf), groups(generate_groups(conf)), overlay_tree(conf.tree), alignment(8 * sizeof(entry_type)),
      dst_field_size(groups.size() / alignment * sizeof(entry_type) + (groups.size() % alignment == 0 ? 0 : sizeof(entry_type))) {

    CHECK(this->conf.pending_len > this->groups.size(), "The pending buffer size (" << this->conf.pending_len
                                                                                    << ") must have more entries than the number of groups ("
                                                                                    << this->groups.size() << ")")
}