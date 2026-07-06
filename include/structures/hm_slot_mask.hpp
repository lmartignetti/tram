#ifndef _HM_SLOT_MASK_
#define _HM_SLOT_MASK_

#include "structures/h_slot_mask.hpp"
#include "structures/m_slot_mask.hpp"

class hm_slot_mask {
  private:
    h_slot_mask h_slot;
    m_slot_mask m_slot;

  public:
    hm_slot_mask( unsigned char *const address, const size_t num_groups, const size_t alignment, const size_t value_size)
        : h_slot(address + m_slot_mask::get_size(value_size), num_groups, alignment), m_slot(address, value_size) {}

    inline h_slot_mask &get_h_slot() noexcept { return this->h_slot; }
    inline m_slot_mask &get_m_slot() noexcept { return this->m_slot; }

    inline  unsigned char *get_address() const noexcept { return this->m_slot.get_address(); }
    inline size_t get_size() const noexcept { return this->m_slot.get_size() + this->h_slot.get_size(); }
    inline static size_t get_size(const size_t num_groups, const size_t alignment, const size_t value_size) noexcept {
        return h_slot_mask::get_size(num_groups, alignment) + m_slot_mask::get_size(value_size);
    }

    inline void set(const hm_slot_mask &hm_slot) noexcept {
        this->h_slot.set(hm_slot.h_slot);
        this->m_slot.set(hm_slot.m_slot);
    }

    std::string to_string() const noexcept {
        std::ostringstream ss;
        ss << "<" << this->h_slot.to_string();
        ss << ", " << this->m_slot.to_string();
        ss << ">";

        return ss.str();
    }
};

#endif /* _HM_SLOT_MASK_ */