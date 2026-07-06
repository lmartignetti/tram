#ifndef _SMCLUSTER_LOGGING_
#define _SMCLUSTER_LOGGING_

#include <cstring>
#include <errno.h>
#include <exception>
#include <iomanip>
#include <iostream>
#include <map>
#include <mutex>
#include <sstream>
#include <vector>

class exception_msg : public std::exception {
  private:
    std::string message;

  public:
    exception_msg(const std::string &msg) : message(msg) {}

    const char *what() const noexcept override { return message.c_str(); }
};

#define __FILENAME__ (strrchr(__FILE__, '/') ? strrchr(__FILE__, '/') + 1 : __FILE__)

extern std::mutex logging_mutex;

#define LOG_GENERAL_ERROR(stream, type)                                                                                                              \
    {                                                                                                                                                \
        logging_mutex.lock();                                                                                                                        \
        std::cerr << "[" << type << "] " << __FILENAME__ << ":" << __LINE__ << ": " << stream << std::endl;                                          \
        std::cerr.flush();                                                                                                                           \
        logging_mutex.unlock();                                                                                                                      \
    }

#define LOG_GENERAL_OUTPUT(stream, type)                                                                                                             \
    {                                                                                                                                                \
        logging_mutex.lock();                                                                                                                        \
        std::cout << "[" << type << "] " << __FILENAME__ << ":" << __LINE__ << ": " << stream << std::endl;                                          \
        std::cout.flush();                                                                                                                           \
        logging_mutex.unlock();                                                                                                                      \
    }

#define LOG_ERROR(stream) LOG_GENERAL_OUTPUT(stream, "ERROR")
#define LOG_WARN(stream) LOG_GENERAL_OUTPUT(stream, "WARN")

#define LOG(stream)                                                                                                                                  \
    {                                                                                                                                                \
        logging_mutex.lock();                                                                                                                        \
        std::cout << stream << std::endl;                                                                                                            \
        std::cout.flush();                                                                                                                           \
        logging_mutex.unlock();                                                                                                                      \
    }

#ifdef DEBUG

#define LOG_INFO(stream) LOG_GENERAL_OUTPUT(stream, "INFO")
#define LOG_DEBUG(stream) LOG_GENERAL_OUTPUT(stream, "DEBUG")

#else

#define LOG_INFO(stream) ;
#define LOG_DEBUG(stream) ;

#endif

// #define CHECK(expr, stream) \
//     { \
//         if (!(expr)) { \
//             int err = errno; \
//             logging_mutex.lock(); \
//             std::cout << "[CHECK] " << __FILENAME__ << ":" << __LINE__ << ": " << stream << std::endl; \
//             std::cout << "[CHECK] " << __FILENAME__ << ":" << __LINE__ << ": " << strerror(errno) << std::endl; \
//             std::cout.flush(); \
//             logging_mutex.unlock(); \
//             throw exception_msg(std::string(__func__) + "(): " + strerror(err)); \
//         } \
//     }

#define CHECK(expr, stream)                                                                                                                          \
    {                                                                                                                                                \
        if (!(expr)) {                                                                                                                               \
            int err = errno;                                                                                                                         \
            std::lock_guard<std::mutex> lock(logging_mutex);                                                                                         \
            std::cerr << "[CHECK] " << __FILENAME__ << ":" << __LINE__ << ": " << stream << "\n";                                                    \
            std::cerr << "[CHECK] errno=" << err << " (" << strerror(err) << ")\n";                                                                  \
            throw exception_msg(std::string(__func__) + ": " + strerror(err));                                                                       \
        }                                                                                                                                            \
    }

// FILENAME: plotdata_<pid>_<plot_counter>_<plot_type>

void log_data(std::vector<double> &data, int pid, std::string custom_flag = "0", std::string title = "title", std::string data_label = "");

/**
 * @brief Log any kind of data
 *
 * @param data data to log
 * @param pid process identifier
 * @param custom_flag custom flag for further parsing
 * @param title title of the log
 * @param data_labels one label for each dataset (defaults to '-')
 */
void log_data(std::vector<std::vector<double>> &data, int pid, std::string custom_flag = "0", std::string title = "title",
              std::vector<std::string> data_labels = {});

// Utility functions

std::string payload_str(size_t size);

template <typename T> inline std::string vector_to_string(const std::vector<T> &vec, bool hex = false, size_t entry_width = 1) {
    std::ostringstream oss;
    if (hex)
        oss << std::hex;
    oss << "[";
    for (size_t i = 0; i < vec.size(); ++i) {
        oss << std::setfill('0') << std::setw(entry_width) << vec[i];
        if (i != vec.size() - 1) {
            oss << ", ";
        }
    }
    oss << "]";
    return oss.str();
}

template <typename T, typename U> inline std::string map_keys_to_string(const std::map<T, U> &map, bool hex = false, size_t entry_width = 1) {
    std::ostringstream oss;
    if (hex)
        oss << std::hex;
    oss << "[";
    for (auto it = map.begin(); it != map.end(); ++it) {
        oss << std::setfill('0') << std::setw(entry_width) << it->first;
        if (it != std::prev(map.end())) {
            oss << ", ";
        }
    }
    oss << "]";
    return oss.str();
}

inline std::string hex_dump(const  void *const address, size_t length) {
    std::stringstream ss;
    for (int i = length - 1; i >= 0; i--)
        for (size_t i = length; i-- > 0;)
            ss << std::hex << std::setfill('0') << std::setw(2) << (int)(( unsigned char *)address)[i];
    ss << std::dec;

    return ss.str();
}

#endif /* _SMCLUSTER_LOGGING_ */