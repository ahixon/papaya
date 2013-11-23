#ifndef __PAGETABLE_H__
#define __PAGETABLE_H__

#include <pawpaw.h>

typedef struct pt_directory* pagetable_t;

#include "addrspace.h"

#define PAGETABLE_L1_BITS   12
#define PAGETABLE_L2_BITS   8

#define PAGETABLE_L1_SIZE   (1 << PAGETABLE_L1_BITS)
#define PAGETABLE_L2_SIZE   (1 << PAGETABLE_L2_BITS)

#define L1_IDX(x)   (x >> (PAGETABLE_L1_BITS + PAGETABLE_L2_BITS))
#define L2_IDX(x)   ((x & 0xFF000) >> PAGETABLE_L1_BITS)

#define PAGE_FAILED     0
#define PAGE_SWAP_IN    1
#define PAGE_SWAP_OUT   2
#define PAGE_SUCCESS    3

/* Describes a mapped page in a virtual address-space.
 *
 * All relevant information about the page itself is inside "frame", since
 * we support shared pages, and this state must be consistent across shared
 * copies as they have the same underlying data.
 */
struct pt_entry {
    struct frameinfo* frame;    /* underlying frame information */
    seL4_CPtr cap;              /* cap used to actually map frame into page,
                                   NULL if not actually mapped */
};

struct pt_table {
    struct pt_entry entries[PAGETABLE_L2_SIZE];
};

struct pt_directory {
    struct pt_table* entries[PAGETABLE_L1_SIZE];

    /* We could have one CPtr inside each pt_table, BUT then size of the struct
     * doesn't fit neatly into frames. However, tradeoff might be less cache 
     * misses due to data locality? TODO: benchmark this! */
    seL4_CPtr table_caps [PAGETABLE_L1_SIZE];
    seL4_Word table_addrs[PAGETABLE_L1_SIZE];
};

struct pagelist {
    struct pt_entry *page;
    struct pagelist *next;
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

int
page_free (struct pt_entry* entry);

struct pt_entry*
page_fetch (pagetable_t pt, vaddr_t vaddr);

struct pt_entry* 
page_fetch_entry (addrspace_t as, seL4_ARM_VMAttributes attributes,
    pagetable_t pt, vaddr_t vaddr);

void pagetable_dump (pagetable_t pt);

struct pt_entry*
page_map_shared (addrspace_t as_dst, struct as_region* reg_dst, vaddr_t dst,
    addrspace_t as_src, struct as_region* reg_src, vaddr_t src, int cow,
    int *status, void* cb, struct pawpaw_event* evt);

#endif
