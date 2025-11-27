#ifndef ATOMIC_H
#define ATOMIC_H

/* from linux/arch/riscv/include/asm/cmpxchg.h */

#include <type.h>

static inline uint32_t atomic_swap(uint32_t val, ptr_t mem_addr)
{
    uint32_t ret;
    __asm__ __volatile__ (
        "amoswap.w.aqrl %0, %2, %1\n"
        : "=r"(ret), "+A" (*(uint32_t*)mem_addr)
        : "r"(val)
        : "memory");
    return ret;
}

static inline uint64_t atomic_swap_d(uint64_t val, ptr_t mem_addr)
{
    uint64_t ret;
    __asm__ __volatile__ (
                          "amoswap.d.aqrl %0, %2, %1\n"
                          : "=r"(ret), "+A" (*(uint64_t*)mem_addr)
                          : "r"(val)
                          : "memory");
    return ret;
}

#endif /* ATOMIC_H */
