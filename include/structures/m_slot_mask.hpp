#ifndef _M_SLOT_MASK_
#define _M_SLOT_MASK_

#include <sstream>

#include "datatypes.hpp"
#include "mem_v.hpp"

class m_slot_mask {
  private:
     unsigned char *const address;
    const size_t value_size;

  public:
    m_slot_mask( unsigned char *const address, const size_t value_size) : address(address), value_size(value_size) {}

    inline bool is_dst(const size_t gid) const noexcept { return (this->address + 2 * sizeof(uint64_t))[gid / 8] & 0x1 << gid % 8; }

    inline  unsigned char *get_address() const noexcept { return this->address; }
    inline  unsigned char *get_value_address() const noexcept { return this->address; }

    inline size_t get_size() const noexcept { return m_slot_mask::get_size(value_size); }
    inline static size_t get_size(const size_t value_size) noexcept { return value_size; }

    inline void set_value(const  unsigned char *const value) noexcept { memcpy_v(this->address, value, m_slot_mask::value_size); }
    inline void set(const m_slot_mask &m_slot) noexcept { memcpy_v(this->address, m_slot.address, this->get_size()); }

    std::string to_string() const noexcept {
        std::ostringstream ss;
        ss << "<[........]>";

        return ss.str();
    }
};

#endif /* _M_SLOT_MASK_ */