/* @LICENSE(NICTA_CORE) */

#ifndef __LIBSEL4_ARCH_CONSTANTS_H
#define __LIBSEL4_ARCH_CONSTANTS_H

#include <autoconf.h>

#define TLS_GDT_ENTRY 6
#define TLS_GDT_SELECTOR ((TLS_GDT_ENTRY << 3) | 3)

#define IPCBUF_GDT_ENTRY 7
#define IPCBUF_GDT_SELECTOR ((IPCBUF_GDT_ENTRY << 3) | 3)

#ifndef __ASM__

typedef enum {
    seL4_IA32_4K = seL4_NonArchObjectTypeCount,
    seL4_IA32_4M,
    seL4_IA32_PageTableObject,
    seL4_IA32_PageDirectoryObject,
#ifdef CONFIG_IOMMU
    seL4_IA32_IOPageTableObject,
#endif
#ifdef CONFIG_VTX
    seL4_IA32_VCPUObject,
    seL4_IA32_EPTPageDirectoryPointerTableObject,
    seL4_IA32_EPTPageDirectoryObject,
    seL4_IA32_EPTPageTableObject,
#endif
    seL4_ObjectTypeCount
} seL4_ArchObjectType;

#endif

#endif
