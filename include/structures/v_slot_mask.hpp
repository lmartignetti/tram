#ifndef _V_SLOT_MASK_
#define _V_SLOT_MASK_

#include <iomanip>
#include <sstream>
#include <vector>

#include "datatypes.hpp"
#include "mem_v.hpp"

class v_slot_mask {
  private:
     unsigned char *const address;

    inline static bool init = false;
    inline static size_t num_groups = 0;
    inline static size_t dst_field_size = 0;

    inline static size_t v_size = 0;

    inline static size_t get_dst_field_size(const size_t num_groups, const size_t alignment) noexcept {
        return num_groups / alignment * sizeof(entry_type) + (num_groups % alignment == 0 ? 0 : sizeof(entry_type));
    }

  public:
    v_slot_mask( unsigned char *const address, const size_t num_groups, const size_t alignment) : address(address) {
        if (!v_slot_mask::init) {
            v_slot_mask::init = true;
            v_slot_mask::num_groups = num_groups;
            v_slot_mask::dst_field_size = get_dst_field_size(num_groups, alignment);
            v_slot_mask::v_size = v_slot_mask::get_size(num_groups, alignment);
        }
    }

    inline bool is_dst(const size_t gid) const noexcept { return this->address[gid / 8] & 0x1 << gid % 8; }

    inline  unsigned char *get_address() const noexcept { return this->address; }
    inline  unsigned char *get_ack_address_at(const size_t group_id) const noexcept {
        return this->address + this->dst_field_size + group_id * sizeof(entry_type);
    }
    inline entry_type get_ack_at(const size_t group_id) const noexcept { return *( entry_type *)(this->get_ack_address_at(group_id)); };

    inline size_t get_size() const noexcept { return this->v_size; }
    inline static size_t get_size(const size_t num_groups, const size_t alignment) noexcept {
        return v_slot_mask::get_dst_field_size(num_groups, alignment) + num_groups * sizeof(entry_type);
    }

    inline void reset() noexcept { memset_v(this->address, 0, this->v_size); }
    inline void set_dst(const std::vector<unsigned int> dst) noexcept {
        for (unsigned int d : dst)
            this->address[d / 8] |= (0x1 << (d % 8));
    }

    std::string to_string() const noexcept {
        std::ostringstream ss;
        ss << "<";
        for (unsigned int g = 0; g < this->num_groups; g++)
            ss << this->is_dst(g) ? '1' : '0';
        ss << ", [";
        for (unsigned int g = 0; g < this->num_groups; g++) {
            ss << std::setw(8) << this->get_ack_at(g);
            if (g < this->num_groups - 1)
                ss << ", ";
        }
        ss << "]>";

        return ss.str();
    }
};

#endif /* _V_SLOT_MASK_ */