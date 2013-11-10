
#ifndef __PAGETABLE_H__
#define __PAGETABLE_H__

#include <pawpaw.h>

typedef struct pt_directory* pagetable_t;

#include "addrspace.h"

#define PAGETABLE_L1_BITS   12
#define PAGETABLE_L2_BITS   8

#define PAGETABLE_L1_SIZE   (1 << PAGETABLE_L1_BITS)
#define PAGETABLE_L2_SIZE   (1 << PAGETABLE_L2_BITS)

#define L1_IDX(x)   (x >> 20)
#define L2_IDX(x)   ((x & 0xFF000) >> 12)

#define PAGE_ALLOCATED      1
#define PAGE_SHARED         2
#define PAGE_COPY_ON_WRITE  4
#define PAGE_SWAPPING       8
#define PAGE_RESERVED       16       /* if you define any more flags, you must increase flag bits in pt_entry struct */

#define PAGE_FAILED     0
#define PAGE_SWAP_IN    1
#define PAGE_SWAP_OUT   2
#define PAGE_SUCCESS    3

struct pt_entry {
    struct frameinfo* frame;
    seL4_CPtr cap;
    unsigned short flags;
    /* there is padding here so feel free to add more data structures */
};

struct pt_table {
    struct pt_entry entries[PAGETABLE_L2_SIZE];
};

struct pt_directory {
    struct pt_table* entries[PAGETABLE_L1_SIZE];

    /* could have one CPtr inside each pt_table, BUT then doesn't fit neatly into
     * frames. however, tradeoff might be caching access? XXX: BENCHMARK */
    seL4_CPtr table_caps [PAGETABLE_L1_SIZE];
    seL4_Word table_addrs[PAGETABLE_L1_SIZE];
};

pagetable_t
pagetable_init (void);

void
pagetable_free (pagetable_t pt);

struct pt_entry*
page_map (addrspace_t as, struct as_region *region, vaddr_t vaddr, int *status,
    void *cb, struct pawpaw_event* evt);

int
page_unmap (struct pt_entry* entry);

void
page_free (pagetable_t pt, vaddr_t vaddr);

struct pt_entry*
page_fetch (pagetable_t pt, vaddr_t vaddr);

struct pt_entry* 
page_fetch_entry (addrspace_t as, seL4_ARM_VMAttributes attributes, pagetable_t pt, vaddr_t vaddr);

void pagetable_dump (pagetable_t pt);

struct pt_entry*
page_map_shared (addrspace_t as_dst, struct as_region* reg_dst, vaddr_t dst,
    addrspace_t as_src, struct as_region* reg_src, vaddr_t src, int cow);

#endif
