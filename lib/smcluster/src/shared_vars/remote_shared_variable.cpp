#include "logging.hpp"
#include "mem_v.hpp"
#include "remote_process/remote_process.hpp"
#include "smcluster.hpp"

remote_shared_variable::remote_shared_variable(void *remote_address, size_t size, uint32_t remote_key, remote_process *p)
    : shared_variable(new (std::align_val_t(8 * sizeof(size_t))) unsigned char[size](), size), remote(p),
      remote_address((unsigned char *)remote_address), remote_key(remote_key) {
    // Allocate the memory region
    try {
        this->mr = p->register_memory_region((void *)this->address, size);
    } catch (const std::exception &e) {
        delete[] this->address;
        throw e;
    }

    LOG_INFO("[REM VAR] Created remote variable (addr: 0x" << std::hex << (uintptr_t)this->remote_address << std::dec << ", size: " << this->size
                                                           << ", rkey: 0x" << std::hex << this->remote_key << std::dec << ")");
}

remote_shared_variable::~remote_shared_variable() {
    LOG_INFO("Destructor of remote_shared_variable: address " << std::hex << (uintptr_t)this->address << std::dec << ", size " << this->size
                                                              << ", remote " << this->remote->get_pid())
    if (ibv_dereg_mr(this->mr) == 0)
        LOG_INFO("Memory region at 0x" << std::hex << (uintptr_t)this->mr << std::dec << " destroyed")
    else
        LOG_ERROR("Failed to destroy memory region at 0x" << std::hex << (uintptr_t)this->mr << std::dec << ": " << strerror(errno))
    ::operator delete[]((void *)this->address, std::align_val_t(8 * sizeof(size_t)));
}

bool remote_shared_variable::read() { return this->wait(this->read_async()); }

uint64_t remote_shared_variable::read_async() { return this->remote->remote_op_handler(remote_op::READ, *this, this->mr); }

bool remote_shared_variable::write() { return this->wait(this->write_async()); }

uint64_t remote_shared_variable::write_async() { return this->remote->remote_op_handler(remote_op::WRITE, *this, this->mr); }

bool remote_shared_variable::wait(uint64_t op_id) {
    uint64_t delay = 0;
    return this->remote->poll_completion_event(op_id, delay);
}

int remote_shared_variable::get_remote() const noexcept { return this->remote->get_pid(); }

std::string remote_shared_variable::to_string() { return hex_dump(this->address, this->size); }
