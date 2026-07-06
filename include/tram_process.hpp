#ifndef _TRAM_PROCESS_
#define _TRAM_PROCESS_

#include "datatypes.hpp"
#include "smcluster.hpp"
#include "tram_parse_config.hpp"
#include "tree.hpp"

class tram_process {
  protected:
    // Input
    smcluster &cluster;
    tram_config conf;
    const std::vector<std::vector<int>> groups;
    tree overlay_tree;

    // Constants
    const size_t alignment; // in bytes

    // Redundant variables
    const size_t dst_field_size;

  public:
    tram_process(smcluster &cluster, const tram_config conf);
};

#endif /* _TRAM_PROCESS_ */