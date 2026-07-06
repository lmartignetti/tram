#ifndef _TREE_HPP
#define _TREE_HPP

#include <cstddef>
#include <vector>

#define ROOT_PARENT -1

class tree {
  private:
    struct node {
        int parent;                         // id of the only parent node
        unsigned int level;                 // level of the node
        std::vector<unsigned int> children; // ids of the children of the node
        std::vector<unsigned int> reach;    // ids of the reachable nodes
    };

    std::vector<node> nodes;

    unsigned int max_level;

  public:
    tree(std::vector<int> parents);

    // find at which level the parent is the same
    unsigned int lowest_common_ancestor(std::vector<unsigned int> groups);

    std::vector<unsigned int> get_children(unsigned int node);

    std::vector<unsigned int> get_reach(unsigned int node);

    size_t get_tree_size();

    bool is_node_valid(unsigned int node);

    int get_parent(unsigned int node);

    unsigned int get_level(unsigned int node);

    unsigned int get_max_level();

    static size_t get_hops(tree overlay_tree, std::vector<unsigned int> dst);

    void print_tree();
};

#endif /* _TREE_HPP */