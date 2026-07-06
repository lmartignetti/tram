#include "mem_v.hpp"

#include <atomic>
#include <stdint.h>
#include <string.h>

void memcpy_v( void *__restrict__ __dest, const  void *__restrict__ __src, size_t __n) noexcept(true) {
     unsigned char *d = ( unsigned char *)__dest;
    const  unsigned char *s = (const  unsigned char *)__src;

    // Copy bytes until d and s are aligned to sizeof(size_t)
    while (__n > 0 && ((uintptr_t)d % sizeof(size_t) != 0 || (uintptr_t)s % sizeof(size_t) != 0)) {
        *d++ = *s++;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        --__n;
    }

    // Copy by larger chunks using size_t (assuming size_t is a good architecture-native size)
     size_t *d_size = ( size_t *)d;
    const  size_t *s_size = (const  size_t *)s;
    while (__n >= sizeof(size_t)) {
        *d_size++ = *s_size++;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        __n -= sizeof(size_t);
    }

    // Copy remaining bytes
    d = ( unsigned char *)d_size;
    s = (const  unsigned char *)s_size;
    while (__n > 0) {
        *d++ = *s++;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        --__n;
    }
}

// memcpy in reverse order. If both dest and src are aligned, then cache line copies are atomic.
void memcpy_v_reverse( void *__restrict__ __dest, const  void *__restrict__ __src, size_t __n) noexcept(true) {
     unsigned char *d = ( unsigned char *)__dest + __n;
    const  unsigned char *s = (const unsigned char *)__src + __n;

    // Copy bytes in reverse order until d and s are aligned to sizeof(size_t)
    while (__n > 0 && ((uintptr_t)d % sizeof(size_t) != 0 || (uintptr_t)s % sizeof(size_t) != 0)) {
        *(--d) = *(--s);
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        --__n;
    }

    // Copy by larger chunks using size_t in reverse order
     size_t *d_size = ( size_t *)d;
    const  size_t *s_size = (const  size_t *)s;
    while (__n >= sizeof(size_t)) {
        *(--d_size) = *(--s_size);
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        __n -= sizeof(size_t);
    }

    // Copy remaining bytes in reverse order
    d = ( unsigned char *)d_size;
    s = (const  unsigned char *)s_size;
    while (__n > 0) {
        *(--d) = *(--s);
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        --__n;
    }
}

void memset_v( void *__s, int __c, size_t __n) noexcept(true) {
     unsigned char *d = ( unsigned char *)__s;
    unsigned char c_byte = (unsigned char)__c;

    // Set bytes until d is aligned to sizeof(size_t)
    while ((uintptr_t)d % sizeof(size_t) != 0 && __n > 0) {
        *d++ = c_byte;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        --__n;
    }

    // Prepare the value to be set as size_t (e.g., filling all bytes in the size_t variable with __c)
    size_t c_word = 0;
    memset(&c_word, c_byte, sizeof(size_t));

    // Set by larger chunks using size_t
     size_t *d_size = ( size_t *)d;
    while (__n >= sizeof(size_t)) {
        *d_size++ = c_word;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        __n -= sizeof(size_t);
    }

    // Set remaining bytes
    d = ( unsigned char *)d_size;
    while (__n > 0) {
        *d++ = c_byte;
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        --__n;
    }
}

int memcmp_v(const  void *__s1, const  void *__s2, size_t __n) noexcept(true) {
    for (size_t i = 0; i < __n; i++) {
        char c1 = (( unsigned char *)(__s1))[i];
        char c2 = (( unsigned char *)(__s2))[i];
        std::atomic_thread_fence(std::memory_order_acquire);
        std::atomic_signal_fence(std::memory_order_acquire);
        if (c1 != c2)
            return -1;
    }

    return 0;
}

bool is_zero_v( void *__s, size_t __n) noexcept(true) {
    for (size_t i = 0; i < __n; i++)
        if ((( unsigned char *)(__s))[i] != 0)
            return false;

    return true;
}
