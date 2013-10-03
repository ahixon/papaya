/* 
 * GIANT FIXME: ALL MALLOC IN THIS FILE SHOULD BE A VMALLOC (which we need to implement)
 *  really only diff is vmalloc would call all these functions but with a special addrspace and
 *  as->pagetable would be initialised in a special way (such that we use initial heap or vaddr that will never be unmapped)
 *
 * two level page table
 * top 20 bits for physical address, bottom for bit flags
 * 
 * original cap stored in frame table
 * create copy, and map into process vspace
 * only delete this original cap on free-ing the frame
 * 
 * if we need to map into SOS, then we map into special window region, copy data in/out, then unmap - DO NOT DELETE 
 * 
 * on deletion of page, just revoke on original cap - this will NOT delete the cap, but all children
 * FIXME: need to check that page is not shared in future (otherwise those mapped pages will have their cap deleted too - basically dangling pointer)
 *
 * by itself: cap for page directory
 * first section of pagetable: pointers to second level
 * second part:                pointers to seL4 page table objects
 * ^ REMEMBER TO HAVE THIS ALL IN FRAMES
 *
 * second level: as above, 20 bit physical addr/index + bit flags
 *
 * might be better to have second level pointer then page table object next to each other for cache access
 */

#include <sel4/sel4.h>
#include <assert.h>
#include <string.h>

#include <cspace/cspace.h>

#include "ut_manager/ut.h"

#include "mapping.h"
#include "vmem_layout.h"
#include "frametable.h"

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

#include "pagetable.h"

void
pagetable_dump (pagetable_t pt) {
    printf ("@@@@ pagetable dump @@@@\n");
    for (int i = 0; i < PAGETABLE_L1_SIZE; i++) {
        struct pt_table* table = pt->entries[i];
        short printed = false;
        if (table == NULL) {
            //printf ("-- unallocated --\n");
            continue;
        }

        printf ("0x%04x: ", i);

        for (int j = 0; j < PAGETABLE_L2_SIZE; j++) {
            struct pt_entry entry = table->entries[j];

            if (entry.flags & PAGE_ALLOCATED) {
                if (printed) {
                    printf ("\t");
                } else {
                    printed = true;
                }


                printf ("\t0x%03x: frame 0x%0x %s\n", j, entry.frame_idx, entry.flags & PAGE_SHARED ? "SHARED" : "" );
            }
        }

        if (!printed) {
            printf ("\n");
        }

    }
    printf ("@@@@@@@@@@@@@@@@@@@@@@@@\n\n");
}

pagetable_t
pagetable_init (void) {

    pagetable_t pt = malloc (sizeof (struct pt_directory));
    if (!pt) {
        printf ("pagetable_init: no memory to alloc pagetable\n");
        return NULL;
    }

    /* we actually map in each page when the time comes (ie when we get a VM fault) */
    memset (pt, 0, sizeof (struct pt_directory));

    return pt;
}

static seL4_Word
_pagetable_map (addrspace_t as, seL4_ARM_VMAttributes attributes, vaddr_t vaddr, seL4_Word* pt_ret_addr) {
    int err;
    seL4_Word pt_cap, pt_addr;

    pt_addr = ut_alloc(seL4_PageTableBits);
    if (pt_addr == 0){
        printf ("_pagetable_map: utalloc failed\n");
        return 0;
    }

    /* Create the frame cap */
    err = cspace_ut_retype_addr(pt_addr, 
                                 seL4_ARM_PageTableObject,
                                 seL4_PageTableBits,
                                 cur_cspace,
                                 &pt_cap);
    if (err) {
        printf ("_pagetable_map: retype failed: %s\n", seL4_Error_Message (err));
        ut_free (pt_addr, seL4_PageTableBits);
        return 0;
    }

    /* Tell seL4 to map the PT in for us */
    //printf ("_pagetable_map: mapping 0x%x ptcap to vaddr 0x%x (0x%x pagedir)\n", pt_cap, vaddr, as->pagedir_cap);
    err = seL4_ARM_PageTable_Map(pt_cap, 
                                 as->pagedir_cap, 
                                 vaddr, 
                                 attributes);

    if (err) {
        printf ("_pagetable_map: seL4_ARM_PageTable_Map failed: %s\n", seL4_Error_Message(err));
        ut_free (pt_addr, seL4_PageTableBits);

        /* if someone's already mapped a pagetable in, don't worry about it */
        if (err != seL4_DeleteFirst) {
            return 0;
        }
    }

    *pt_ret_addr = pt_addr;
    return pt_cap;
}

static int
_page_map (vaddr_t vaddr, frameidx_t frame, struct as_region* region, addrspace_t as) {
    int err;
    seL4_Word frame_cap, dest_cap;

    frame_cap = frametable_fetch_cap (frame);
    assert (frame_cap);

    /* FIXME: if you wish to map across multiple processes, YOU NEED TO COPY THE CAP
     * AND STORE IT SOMEWHERE IN THE PAGE ENTRY AND UPDATE THE FREE FUNCTION! */
    dest_cap = frame_cap;

    //printf ("_page_map: mapping cap (originally 0x%x, copy is 0x%x) to vaddr 0x%x\n", frame_cap, dest_cap, vaddr);
    //printf ("           perms = %d, attrib = %d, pagedir cap = 0x%x\n", region->permissions, region->attributes, as->pagedir_cap);
    err = seL4_ARM_Page_Map(dest_cap, as->pagedir_cap, vaddr, region->permissions, region->attributes);

    if (err) {
        printf ("_page_map: failed to map page: %s\n", seL4_Error_Message (err));
        return false;
    }

    return true;
}

/* Fetches the PTE from the pagetable assocated with a virtual address. 
 * If it does not exist, it is created .
 *
 * Returns NULL on failure.
 */
struct pt_entry* 
page_fetch_entry (addrspace_t as, seL4_ARM_VMAttributes attributes, pagetable_t pt, vaddr_t vaddr) {
    int l1 = L1_IDX (vaddr);
    struct pt_table* table = pt->entries[l1];
    if (!table) {
        seL4_Word pt_cap, pt_addr;

        //printf ("page_map: mallocing new PT in page dir at idx %d\n", l1);
        table = malloc (sizeof (struct pt_table));
        if (!table) {
            printf ("page_map: malloc failed\n");
            return NULL;
        }

        memset (table, 0, sizeof (struct pt_table));

        /* now create the capability */
        pt_cap = _pagetable_map (as, attributes, vaddr, &pt_addr);
        if (!pt_cap) {
            printf ("pagetable map failed\n");
            free (table);
            return NULL;
        }

        pt->table_caps [l1] = pt_cap;
        pt->entries    [l1] = table;
        pt->table_addrs[l1] = pt_addr;  /* so we can free back to ut allocator */
    }

    int l2 = L2_IDX (vaddr);
    struct pt_entry* entry = &(table->entries[l2]);

    return entry;
}

/*
 * you probably want this to do the share_vm syscall
 * FIXME: man, massive hack
 * basically we want to MOUNT not create two pages, so fix this please
 */
struct pt_entry*
page_map_shared (addrspace_t as_dst, struct as_region* reg_dst, vaddr_t dst,
    addrspace_t as_src, struct as_region* reg_src, vaddr_t src, int cow) {

    struct pt_entry* src_entry = page_fetch_entry (as_src, reg_dst->attributes, as_src->pagetable, src);
    struct pt_entry* dst_entry = page_fetch_entry (as_dst, reg_dst->attributes, as_dst->pagetable, dst);

    src_entry->flags |= PAGE_SHARED;
    if (cow) {
        src_entry->flags |= PAGE_COPY_ON_WRITE;
        /* FIXME: remap page and read-only */
    }

    /* now allocate/mark an entry in dest, and set to same physical frame */
    /*if (dst_entry->flags & PAGE_ALLOCATED) {
        printf ("page_map_share: WARNING! dest page 0x%x already allocated at idx 0x%x! freeing\n", dst, dst_entry->frame_idx);
        //frame_free (dst_entry->frame_idx);
    }*/

    /* FIXME: will copy the same permissions from source region to dest region page - IS THIS OK? */
    //_page_map (dst, src_entry->frame_idx, reg_src, as_dst);

    /* use previous frame */

    printf ("\t* allocating page + frame for 0x%x\n", src);
    if (!(src_entry->flags & PAGE_ALLOCATED)) {
        printf ("\t* page wasn't allocated yet??\n");
        page_map (as_src, reg_src, src);
    }

    dst_entry->frame_idx = src_entry->frame_idx;
    dst_entry->flags = src_entry->flags;

    if (!_page_map (dst, dst_entry->frame_idx, reg_dst, as_dst)) {
        printf ("page_map_share: failed to allocate source page\n");
        return NULL;
    }

    printf ("source pagetable:\n");
    pagetable_dump (as_src->pagetable);

    printf ("source addrspace:\n");
    addrspace_print_regions (as_src);

    printf ("\ndest pagetable:\n");
    pagetable_dump (as_dst->pagetable);

    printf ("dest addrspace:\n");
    addrspace_print_regions (as_dst);
    printf ("\n");

    printf ("BY THE WAY, was linking 0x%x and 0x%x\n", src, dst);

    return dst_entry;
}

/*
 * CALLER should double check that the vaddr is in a valid region
 */
frameidx_t
page_map (addrspace_t as, struct as_region* region, vaddr_t vaddr) {
    assert (as != NULL);

    pagetable_t pt = as->pagetable;
    assert (pt != NULL);

    struct pt_entry* entry = page_fetch_entry (as, region->attributes, pt, vaddr);
    if (!entry) {
        return 0;
    }

    if (entry->flags & PAGE_ALLOCATED) {
        printf ("page_map: page at vaddr 0x%x already allocated!\n", vaddr);
        return entry->frame_idx;
    }

    /* ok now try to map in addrspace */
    frameidx_t frame = frame_alloc ();
    if (!frame) {
        printf ("page_map: no memory left\n");
        return 0;
    }
    
    if (!_page_map (vaddr, frame, region, as)) {
        printf ("actual page map failed\n");
        frame_free (frame);
        return 0;
    }

    entry->frame_idx = frame;
    entry->flags |= PAGE_ALLOCATED;

    return frame;
}

struct pt_entry*
page_fetch (pagetable_t pt, vaddr_t vaddr) {
    uint32_t l1 = L1_IDX(vaddr);
    uint32_t l2 = L2_IDX(vaddr);

    struct pt_table* table = pt->entries[l1];
    if (!table) {
        return NULL;
    }

    struct pt_entry* entry = &(table->entries[l2]);
    return entry;
}

#if 0
void
page_free (pagetable_t pt, vaddr_t vaddr) {
    struct pt_entry* entry = page_fetch (pt, vaddr);
    if (!entry) {
        return;
    }

    if (!(entry->flags & PAGE_ALLOCATED)) {
        return;
    }

    /* FIXME: what about shared pages! needs refcount */

    entry->flags &= ~PAGE_ALLOCATED;
    frame_free (entry->frame_idx);
}
#endif

void
pagetable_free (pagetable_t pt) {
    for (uint32_t l1 = 0; l1 < PAGETABLE_L1_SIZE; l1++) {
        struct pt_table* table = pt->entries[l1];
        if (table) {
            for (uint32_t l2 = 0; l2 < PAGETABLE_L2_SIZE; l2++) {
                struct pt_entry* entry = &(table->entries[l2]);

                /* FIXME: refcount the page here? */
                if (entry->flags & PAGE_ALLOCATED) {
                    entry->flags &= ~PAGE_ALLOCATED;

                    /* FIXME: revoke initial cap instead? - will destroy our copy but not the original
                     * WHAT DOES THIS MEAN?! */
                    frame_free (entry->frame_idx);
                }
            }

            /* now free the table cap + addrs */
            cspace_delete_cap (cur_cspace, pt->table_caps [l1]);
            ut_free (pt->table_addrs[l1], seL4_PageTableBits);
            free (table);
        }
    }

    /* free page dir */
    free (pt);
}