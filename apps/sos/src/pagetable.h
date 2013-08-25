#ifndef __PAGETABLE_H__
#define __PAGETABLE_H__


typedef struct pt_directory* pagetable_t;

#include "addrspace.h"

#define PAGETABLE_L1_BITS   12
#define PAGETABLE_L2_BITS   8

#define PAGETABLE_L1_SIZE   (1 << PAGETABLE_L1_BITS)
#define PAGETABLE_L2_SIZE   (1 << PAGETABLE_L2_BITS)

#define L1_IDX(x)   (x >> 20)
#define L2_IDX(x)   ((x & 0xFF000) >> 12)

#define PAGE_ALLOCATED  1

struct pt_entry {
    /* FIXME: is it better to use 4 (== 24 bits) or round up to 12 and use 32 bits
     * for cache line optimisation? */
    frameidx_t frame_idx    : (PAGETABLE_L1_BITS + PAGETABLE_L2_BITS);
    unsigned short flags    : 4;
};

struct pt_table {
    /* requires 0.25 frames */
    struct pt_entry entries[PAGETABLE_L2_SIZE];
};

struct pt_directory {
    /* requires 8 frames - frame aligned */
    struct pt_table* entries[PAGETABLE_L1_SIZE];

    /* could have one CPtr inside each pt_table, BUT then doesn't fit neatly into
     * frames. however, tradeoff might be caching access? BENCHMARK */
    seL4_CPtr table_caps[PAGETABLE_L1_SIZE];
};

pagetable_t
pagetable_init (void);

void
pagetable_free (pagetable_t pt);

frameidx_t
page_map (struct addrspace * as, struct as_region * region, vaddr_t vaddr);

void
page_free (pagetable_t pt, vaddr_t vaddr);

struct pt_entry*
page_fetch (pagetable_t pt, vaddr_t vaddr);

#endif