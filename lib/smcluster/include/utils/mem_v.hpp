#ifndef _MEM_V_
#define _MEM_V_

#include <stddef.h>

void memcpy_v( void *__restrict__ __dest, const  void *__restrict__ __src, size_t __n) noexcept(true);
void memcpy_v_reverse( void *__restrict__ __dest, const  void *__restrict__ __src, size_t __n) noexcept(true);
void memset_v( void *__s, int __c, size_t __n) noexcept(true);
int memcmp_v(const  void *__s1, const  void *__s2, size_t __n) noexcept(true);
bool is_zero_v( void *__s, size_t __n) noexcept(true);

#endif /* _MEM_V_ */
