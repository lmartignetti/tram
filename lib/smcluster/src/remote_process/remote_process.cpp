#include <algorithm>
#include <chrono>
#include <netdb.h>
#include <unistd.h>

#include "logging.hpp"
#include "remote_process.hpp"

#include "utils.hpp"

// set the timeout to allow for longer reads/writes
#define CONNECTION_TIMEOUT 3000 // ms
#define NUM_RETRIES 10

remote_process::remote_process(int pid, smcluster &cluster, int sockfd, const std::shared_ptr<struct rdma_remote_resources> res)
    : pid(pid), cluster(cluster), sockfd(sockfd), res(res) {
    this->next_work_request_id = 0;
    LOG_INFO("Remote process initialized with PID: " << this->pid << std::endl)
}

// Communication methods

void remote_process::sock_sync_data(int xfer_size, const void *local_data, void *remote_data) {
    this->write_all(sockfd, local_data, xfer_size);
    this->read_all(sockfd, remote_data, xfer_size);
}

// Write all bytes to the socket
void remote_process::write_all(int socket_fd, const void *buffer, size_t length) {
    const char *ptr = static_cast<const char *>(buffer);
    size_t bytes_written = 0;

    while (bytes_written < length) {
        ssize_t result = write(socket_fd, ptr + bytes_written, length - bytes_written);
        CHECK(result >= 0, "Write of " + std::to_string(length) + " bytes failed after writing " + std::to_string(bytes_written) + " bytes")
        bytes_written += result;
    }
    CHECK(bytes_written == length, "Write of " + std::to_string(length) + " bytes failed: " + std::to_string(bytes_written) + " bytes written")
}

// Read all bytes from the socket until the desired length is reached
void remote_process::read_all(int socket_fd, void *buffer, size_t length) {
    char *ptr = static_cast<char *>(buffer);
    size_t bytes_read = 0;

    while (bytes_read < length) {
        ssize_t result = read(socket_fd, ptr + bytes_read, length - bytes_read);
        CHECK(result >= 0, "Read of " + std::to_string(length) + " bytes failed after reading " + std::to_string(bytes_read) + " bytes")
        CHECK(result != 0, "Read of " + std::to_string(length) + " bytes failed: connection closed by peer")
        bytes_read += result;
    }
    CHECK(bytes_read == length, "Read of " + std::to_string(length) + " bytes failed: " + std::to_string(bytes_read) + " bytes read")
}

// Utility

std::vector<remote_shared_variable *> remote_process::get_remote_variables() {
    std::vector<remote_shared_variable *> v;
    for (const auto &r : this->remote_shared_variables)
        v.push_back(r.get());

    return v;
}
