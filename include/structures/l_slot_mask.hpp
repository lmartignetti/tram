#ifndef _L_SLOT_MASK_
#define _L_SLOT_MASK_

#include <sstream>

#include "datatypes.hpp"
#include "logging.hpp"
#include "mem_v.hpp"
#include "structures/hm_slot_mask.hpp"

class l_slot_mask {
  private:
     unsigned char *const address;
    const size_t l_size;
    hm_slot_mask hm_slot;

  public:
    l_slot_mask( unsigned char *const address, const size_t num_groups, const size_t alignment, const size_t value_size)
        : address(address), hm_slot(address + 4 * sizeof(entry_type), num_groups, alignment, value_size),
          l_size(l_slot_mask::get_size(num_groups, alignment, value_size)) {}

    inline  unsigned char *get_address() const noexcept { return this->address; }
    inline size_t get_rnd() const noexcept { return *( size_t *)this->address; }
    inline size_t get_seq_l() const noexcept { return *( size_t *)(this->address + sizeof(size_t)); }
    inline size_t get_ack_c() const noexcept { return *( size_t *)(this->address + 2 * sizeof(size_t)); }
    inline size_t get_source() const noexcept { return *( size_t *)(this->address + 3 * sizeof(size_t)); }
    inline hm_slot_mask &get_hm_slot() noexcept { return this->hm_slot; }

    inline size_t get_size() const noexcept { return this->l_size; }
    inline static size_t get_size(const size_t num_groups, const size_t alignment, const size_t value_size) noexcept {
        return 4 * sizeof(entry_type) + hm_slot_mask::get_size(num_groups, alignment, value_size);
    }

    inline void set_rnd(size_t rnd) noexcept { *( size_t *)this->address = rnd; }
    inline void set_seq(size_t seq) noexcept { *( size_t *)(this->address + sizeof(size_t)) = seq; }
    inline void set_ack_c(size_t ack_c) noexcept { *( size_t *)(this->address + 2 * sizeof(size_t)) = ack_c; }
    inline void set_source(size_t source) noexcept { *( size_t *)(this->address + 3 * sizeof(size_t)) = source; }

    inline void set(const l_slot_mask &l_slot) noexcept { memcpy_v(this->address, l_slot.address, this->l_size); }

    std::string to_string() const noexcept {
        std::ostringstream ss;
        ss << "<" << this->get_rnd();
        ss << ", " << this->get_seq_l();
        ss << ", " << this->get_ack_c();
        ss << ", " << this->get_source();
        ss << ", " << this->hm_slot.to_string();
        ss << ">";

        return ss.str();
    }
};

#endif /* _L_SLOT_MASK_ */