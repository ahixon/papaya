/* @LICENSE(NICTA_CORE) */

#ifndef _CPUID_H_
#define _CPUID_H_

/* read ID register from CPUID */
static inline uint32_t read_cpuid_id(void)
{
    uint32_t val;
    asm volatile("mrc p15, 0, %0, c0, c0, 0" : "=r" (val) :: "cc");
    return val;
}

/* read MP ID register from CPUID */
static inline uint32_t read_cpuid_mpidr(void)
{
    uint32_t val;
    asm volatile("mrc p15, 0, %0, c0, c0, 5" : "=r" (val) :: "cc");
    return val;
}

#endif /* _CPUID_H_ */

