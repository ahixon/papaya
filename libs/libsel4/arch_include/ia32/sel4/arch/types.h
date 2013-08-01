/* @LICENSE(NICTA_CORE) */

#ifndef __LIBSEL4_ARCH_TYPES_H
#define __LIBSEL4_ARCH_TYPES_H

#include <stdint.h>

#define seL4_WordBits        32
#define seL4_PageBits        12
#define seL4_4MBits          22
#define seL4_SlotBits         4
#define seL4_TCBBits         10
#define seL4_EndpointBits     4
#define seL4_PageTableBits   12
#define seL4_PageDirBits     12
#define seL4_IOPageTableBits 12

typedef uint32_t  seL4_Word;
typedef seL4_Word seL4_CPtr;

typedef seL4_CPtr seL4_IA32_IOSpace;
typedef seL4_CPtr seL4_IA32_IOPort;
typedef seL4_CPtr seL4_IA32_Page;
typedef seL4_CPtr seL4_IA32_PageDirectory;
typedef seL4_CPtr seL4_IA32_PageTable;
typedef seL4_CPtr seL4_IA32_IOPageTable;
typedef seL4_CPtr seL4_IA32_VCPU;
typedef seL4_CPtr seL4_IA32_EPTPageDirectoryPointerTable;
typedef seL4_CPtr seL4_IA32_EPTPageDirectory;
typedef seL4_CPtr seL4_IA32_EPTPageTable;
typedef seL4_CPtr seL4_IA32_IPI;

/* User context as used by seL4_TCB_ReadRegisters / seL4_TCB_WriteRegisters */

typedef struct {
    /* frameRegisters */
    seL4_Word eip, esp, eflags, eax, ebx, ecx, edx, esi, edi, ebp;
    /* gpRegisters */
    seL4_Word tls_base, fs, gs;
} seL4_UserContext;

typedef enum {
    seL4_IA32_Default_VMAttributes = 0,
    seL4_IA32_WriteBack = 0,
    seL4_IA32_WriteThrough = 1,
    seL4_IA32_CacheDisabled = 2,
    seL4_IA32_Uncacheable = 3,
    seL4_IA32_WriteCombining = 4,
    SEL4_FORCE_LONG_ENUM(seL4_IA32_VMAttributes),
} seL4_IA32_VMAttributes;

#endif
