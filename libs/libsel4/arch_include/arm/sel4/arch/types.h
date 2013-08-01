/* @LICENSE(NICTA_CORE) */

#ifndef __LIBSEL4_ARCH_TYPES_H
#define __LIBSEL4_ARCH_TYPES_H

#include <sel4/macros.h>
#include <stdint.h>

#define seL4_WordBits 32

#define seL4_PageBits 12
#define seL4_SlotBits 4
#define seL4_TCBBits 9
#define seL4_EndpointBits 4
#define seL4_PageTableBits 10
#define seL4_PageDirBits 14

#define seL4_Frame_Args 4
#define seL4_Frame_MRs 7
#define seL4_Frame_HasNPC 0

typedef uint32_t seL4_Word;
typedef seL4_Word seL4_CPtr;

typedef seL4_CPtr seL4_ARM_Page;
typedef seL4_CPtr seL4_ARM_PageTable;
typedef seL4_CPtr seL4_ARM_PageDirectory;

typedef struct {
    /* frame registers */
    seL4_Word pc, sp, cpsr, r0, r1, r8, r9, r10, r11, r12;
    /* other integer registers */
    seL4_Word r2, r3, r4, r5, r6, r7, r14;
} seL4_UserContext;

typedef enum {
    seL4_ARM_PageCacheable = 0x01,
    seL4_ARM_ParityEnabled = 0x02,
    seL4_ARM_Default_VMAttributes =
        seL4_ARM_PageCacheable | seL4_ARM_ParityEnabled,
    SEL4_FORCE_LONG_ENUM(seL4_ARM_VMAttributes),
} seL4_ARM_VMAttributes;

#endif /* __ARCH_SEL4TYPES_H__ */
