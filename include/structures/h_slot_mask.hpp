#ifndef _H_SLOT_MASK_
#define _H_SLOT_MASK_

#include <sstream>
#include <vector>

#include "datatypes.hpp"
#include "mem_v.hpp"

class h_slot_mask {
  private:
     unsigned char *const address;

    inline static bool init = false;
    inline static size_t num_groups = 0;
    inline static size_t dst_field_size = 0;
    inline static size_t h_size = 0;

    inline static size_t get_dst_field_size(const size_t num_groups, const size_t alignment) noexcept {
        return num_groups / alignment * sizeof(entry_type) + (num_groups % alignment == 0 ? 0 : sizeof(entry_type));
    }

  public:
    h_slot_mask( unsigned char *const address, const size_t num_groups, const size_t alignment) : address(address) {
        if (!h_slot_mask::init) {
            h_slot_mask::init = true;
            h_slot_mask::num_groups = num_groups;
            h_slot_mask::dst_field_size = h_slot_mask::get_dst_field_size(num_groups, alignment);
            h_slot_mask::h_size = h_slot_mask::get_size(num_groups, alignment);
        }
    }

    inline  unsigned char *get_address() const noexcept { return this->address; }
    inline timestamp get_msg_id() const noexcept {
        uint64_t pid = *reinterpret_cast< uint64_t *>(this->address);
        uint64_t counter = *reinterpret_cast< uint64_t *>(this->address + sizeof(uint64_t));
        timestamp ts = {pid, counter};
        return ts;
    }
    inline  unsigned char *get_dst_address() const noexcept { return this->address + 2 * sizeof(uint64_t); }
    inline bool is_dst(const size_t gid) const noexcept { return (this->address + 2 * sizeof(uint64_t))[gid / 8] & 0x1 << gid % 8; }
    inline entry_type get_vptr() const noexcept {
        return *( entry_type *)(this->address + 2 * sizeof(uint64_t) + h_slot_mask::dst_field_size);
    }
    inline entry_type get_seq() const noexcept {
        return *( entry_type *)(this->address + 2 * sizeof(uint64_t) + h_slot_mask::dst_field_size + sizeof(entry_type));
    };

    inline size_t get_size() const noexcept { return h_slot_mask::h_size; }
    inline static size_t get_size(const size_t num_groups, const size_t alignment) noexcept {
        return 2 * sizeof(uint64_t) + h_slot_mask::get_dst_field_size(num_groups, alignment) + 2 * sizeof(entry_type);
    }

    inline void set_msg_id(const timestamp &msg_id) noexcept {
        memcpy_v(this->address, &msg_id.pid, sizeof(uint64_t));
        memcpy_v(this->address + sizeof(uint64_t), &msg_id.counter, sizeof(uint64_t));
    }
    inline void set_dst(const  void *const dst) noexcept { memcpy_v(this->address + 2 * sizeof(uint64_t), dst, h_slot_mask::dst_field_size); }
    inline void set_dst(const std::vector<unsigned int> dst) noexcept {
        memset_v(this->address + 2 * sizeof(uint64_t), 0, this->dst_field_size);
        for (unsigned int d : dst)
            (this->address + 2 * sizeof(uint64_t))[d / 8] |= (0x1 << (d % 8));
    }
    inline void set_vptr(const entry_type vptr) noexcept {
        *(( entry_type *)(this->address + 2 * sizeof(uint64_t) + h_slot_mask::dst_field_size)) = vptr;
    }
    inline void set_seq(const entry_type seq) noexcept {
        *(( entry_type *)(this->address + 2 * sizeof(uint64_t) + h_slot_mask::dst_field_size + sizeof(entry_type))) = seq;
    }
    inline void set(const h_slot_mask &h_slot) noexcept { memcpy_v(this->address, h_slot.address, h_slot_mask::h_size); }

    std::string to_string() const noexcept {
        std::ostringstream ss;
        ss << "<";
        timestamp msg_id = this->get_msg_id();
        ss << ts_to_string(msg_id);
        ss << ", ";
        for (unsigned int g = 0; g < h_slot_mask::num_groups; g++)
            ss << this->is_dst(g) ? '1' : '0';
        ss << ", " << this->get_vptr();
        ss << ", " << this->get_seq();
        ss << ">";

        return ss.str();
    }
};

#endif /* _H_SLOT_MASK_ */