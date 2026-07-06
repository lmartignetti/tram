#include "tree.hpp"

#include "logging.hpp"

tree::tree(std::vector<int> parents) {
    // check that groups in the tree are valid
    for (int p : parents)
        CHECK((p >= 0 && p < parents.size()) || (p == ROOT_PARENT), "Group " + std::to_string(p) + " is not in the network!")

    // find the level of each group (distance to the root)
    this->max_level = 0;
    for (size_t gid = 0; gid < parents.size(); gid++) {
        unsigned int level = 0;
        int parent = parents.at(gid);
        while (parent != ROOT_PARENT) {
            parent = parents.at(parent);
            level++;
        }
        this->nodes.push_back({parents.at(gid), level, {}});
        this->max_level = std::max(this->max_level, level);
    }

    // compute the children of each node
    for (size_t gid = 0; gid < this->nodes.size(); gid++) {
        for (size_t child = 0; child < this->nodes.size(); child++) {
            if (this->nodes.at(child).parent == gid) {
                this->nodes.at(gid).children.push_back(child);
            }
        }
    }

    // compute the reach of each node
    for (size_t gid = 0; gid < this->nodes.size(); gid++) {
        for (int maybe_in_reach = 0; maybe_in_reach < this->nodes.size(); maybe_in_reach++) {
            bool in_reach = false;
            int parent = maybe_in_reach;
            do {
                if (parent == gid) {
                    this->nodes.at(gid).reach.push_back(maybe_in_reach);
                    break;
                }
                parent = this->nodes.at(parent).parent;
            } while (parent != ROOT_PARENT);
        }
    }

#ifdef DEBUG
    LOG_INFO("Tree created")
    LOG_INFO("id, parent, level, children, reach")
    for (size_t i = 0; i < this->nodes.size(); i++) {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(2) << i << " ";
        ss << std::setfill('0') << std::setw(2) << this->nodes.at(i).parent << " ";
        ss << std::setfill('0') << std::setw(2) << this->nodes.at(i).level << " ";
        ss << "[";
        for (size_t child : this->nodes.at(i).children)
            ss << std::setfill('0') << std::setw(2) << child << " ";
        ss << "] [";
        for (size_t reached : this->nodes.at(i).reach)
            ss << std::setfill('0') << std::setw(2) << reached << " ";
        ss << "]";
        LOG_INFO(ss.str())
    }
#endif
}

unsigned int tree::lowest_common_ancestor(std::vector<unsigned int> groups) {
    // check preconditions
    CHECK(!groups.empty(), "At least one group must be specified!")

    for (unsigned int g : groups)
        CHECK(is_node_valid(g), "Group " + std::to_string(g) + " is not in the network!")

    // find the minimum level
    unsigned int minLevel = this->get_level(groups.at(0));
    for (unsigned int g : groups)
        minLevel = std::min(minLevel, this->get_level(g));

    // retrieve the parent nodes at the minimum level
    for (unsigned int &g : groups)
        while (this->get_level(g) > minLevel)
            g = this->get_parent(g); // go up the tree

    // go up the tree until the parent is the same
    bool sameParent;
    do {
        // check if the common parent is found
        sameParent = true;
        for (unsigned int g : groups)
            if (g != groups.at(0)) {
                sameParent = false;
                break;
            }

        // if the current parent nodes are different, go up one level
        if (!sameParent)
            for (unsigned int &g : groups)
                g = this->get_parent(g);
    } while (!sameParent);

    return groups.at(0);
}

std::vector<unsigned int> tree::get_children(unsigned int node) { return this->nodes.at(node).children; }

std::vector<unsigned int> tree::get_reach(unsigned int node) { return this->nodes.at(node).reach; }

size_t tree::get_tree_size() { return this->nodes.size(); }

bool tree::is_node_valid(unsigned int node) { return !(node < 0 || node >= this->nodes.size()); }

int tree::get_parent(unsigned int node) {
    CHECK(is_node_valid(node), "Node id " + std::to_string(node) + " out of bound (max: " + std::to_string(this->nodes.size()) + ")")
    return this->nodes.at(node).parent;
}

unsigned int tree::get_level(unsigned int node) {
    CHECK(is_node_valid(node), "Node id " + std::to_string(node) + " out of bound (max: " + std::to_string(this->nodes.size()) + ")")
    return this->nodes.at(node).level;
}

unsigned int tree::get_max_level() { return this->max_level; }

size_t tree::get_hops(tree overlay_tree, std::vector<unsigned int> dst) {
    unsigned int lca_level = overlay_tree.get_level(overlay_tree.lowest_common_ancestor(dst));
    unsigned int max_level = 0;
    for (unsigned int d : dst)
        max_level = std::max(max_level, overlay_tree.get_level(d));
    return max_level - lca_level;
}

void tree::print_tree() {
    std::cout << "id, parent, level, children, reach" << std::endl;
    for (size_t i = 0; i < this->nodes.size(); i++) {
        std::stringstream ss;
        ss << std::setfill('0') << std::setw(2) << i << " ";
        ss << std::setfill('0') << std::setw(2) << this->nodes.at(i).parent << " ";
        ss << std::setfill('0') << std::setw(2) << this->nodes.at(i).level << " ";
        ss << "[";
        for (size_t child : this->nodes.at(i).children)
            ss << std::setfill('0') << std::setw(2) << child << " ";
        ss << "] [";
        for (size_t reached : this->nodes.at(i).reach)
            ss << std::setfill('0') << std::setw(2) << reached << " ";
        ss << "]" << std::endl;
        std::cout << ss.str();
    }

    std::cout << "Probability of each node (col idx) being lca given number of destinations (row idx)" << std::endl;
    const size_t g = this->nodes.size();
    std::vector<std::vector<double>> prob_lca; // [num_dest][node_id]
    for (size_t d = 1; d <= g; d++) {
        prob_lca.push_back({});
        for (size_t x = 0; x < g; x++) {
            uint64_t R_x = this->nodes.at(x).reach.size(); // Size of reach of node x

            double prob_lca_x;
            if (R_x < d)
                prob_lca_x = 0.0;
            else {
                uint64_t num = 1.0;
                uint64_t den = 1.0;
                for (size_t i = 0; i < d; i++) {
                    num *= (R_x - i);
                    den *= (g - i);
                }
                double p_a = double(num) / double(den);

                double p_b = double(d) / double(R_x);

                double p_c = 1.0;
                if (R_x > d) {
                    num = 0.0;
                    den = 1.0;
                    for (unsigned int i : this->get_children(x)) {
                        uint64_t R_i = this->nodes.at(i).reach.size(); // Size of reach of node i
                        if (R_i >= d) {
                            uint64_t num_addend = 1.0;
                            for (size_t j = 0; j < d; j++)
                                num_addend *= (R_i - j);
                            num += num_addend;
                        }
                    }
                    for (size_t i = 0; i < d; i++)
                        den *= (R_x - 1 - i);
                    p_c = 1 - double(num) / double(den);
                }

                prob_lca_x = p_a * (p_b + (1.0 - p_b) * p_c);
            }
            prob_lca.back().push_back(prob_lca_x);
        }
    }

    std::cout << std::setw(7) << " ";
    for (size_t x = 0; x < g; x++) {
        std::cout << std::setw(7) << x << " ";
    }
    std::cout << std::endl;
    for (size_t d = 1; d <= g; d++) {
        std::cout << std::setw(7) << d << " ";
        for (size_t x = 0; x < g; x++) {
            std::cout << std::setw(7) << std::fixed << std::setprecision(3) << prob_lca.at(d - 1).at(x) << " ";
        }
        std::cout << std::endl;
    }
}
