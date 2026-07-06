#include "datatypes.hpp"

#include <iomanip>
#include <iostream>
#include <sstream>

#include "mem_v.hpp"

std::string ts_to_string(const  timestamp &ts, bool fixed_width) {
    std::ostringstream ss;
    if (fixed_width) {
        ss << "(" << std::dec << std::setfill('0') << std::setw(2) << ts.pid << ", ";
        ss << std::hex << std::setfill('0') << std::setw(2 * sizeof(entry_type)) << ts.counter << ")" << std::dec;
    } else {
        ss << "(" << std::dec << ts.pid << ", ";
        ss << std::hex << ts.counter << ")" << std::dec;
    }
    return ss.str();
}

void pin_thread_to_core(int core_id) {
    cpu_set_t cpuset;
    CPU_ZERO(&cpuset);
    CPU_SET(core_id, &cpuset);

    pthread_t current_thread = pthread_self();
    pthread_setaffinity_np(current_thread, sizeof(cpu_set_t), &cpuset);
}
