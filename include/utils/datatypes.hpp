#ifndef _TRAM_DATATYPES_HPP_
#define _TRAM_DATATYPES_HPP_

#include <inttypes.h>
#include <stddef.h>
#include <string>

typedef uint64_t entry_type;

struct timestamp {
    entry_type pid;
    entry_type counter;
};

std::string ts_to_string(const  timestamp &ts, bool fixed_width = false);

void pin_thread_to_core(int core_id);

// class remote_variable {
//   private:
//      unsigned char *address; // local address
//     const size_t length;             // size
//     const size_t pos;                // position index in the sequence of variables shared from the same remote

//   public:
//     remote_variable(const  void *address, const size_t length, size_t &pos);

//      unsigned char *get_address() const;
//     size_t get_size() const;
//     size_t get_pos() const;
// };

#endif /* _TRAM_DATATYPES_HPP_ */