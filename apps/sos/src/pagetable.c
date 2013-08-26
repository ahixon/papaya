/* 
 * GIANT FIXME: ALL MALLOC IN THIS FILE SHOULD BE A VMALLOC (which we need to implement)
 *  really only diff is vmalloc would call all these functions but with a special addrspace and
 *  as->pagetable would be initialised in a special way (such that we use initial heap or vaddr that will never be unmapped)
 *
 * GIANT FIXME: leaking memory on ut_alloc stuff! :(
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
_pagetable_map (addrspace_t as, seL4_ARM_VMAttributes attributes, vaddr_t vaddr) {
    int err;
    seL4_Word pt_addr, pt_cap;

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

    return pt_cap;
}

static int
_page_map (vaddr_t vaddr, frameidx_t frame, struct as_region* region, addrspace_t as) {
    int err;
    seL4_Word frame_cap, dest_cap;

    frame_cap = frametable_fetch_cap (frame);
    assert (frame_cap);

    /* copy the cap (should we badge?) - FIXME: ALSO WHICH CSPACE DO WE WANT */
    dest_cap = cspace_copy_cap(cur_cspace, cur_cspace, frame_cap, seL4_AllRights);
    if (!dest_cap) {
        printf ("_page_map: failed to copy cap\n");
        return false;
    }

    //printf ("_page_map: mapping cap (originally 0x%x, copy is 0x%x) to vaddr 0x%x\n", frame_cap, dest_cap, vaddr);
    //printf ("           perms = %d, attrib = %d, pagedir cap = 0x%x\n", region->permissions, region->attributes, as->pagedir_cap);
    err = seL4_ARM_Page_Map(dest_cap, as->pagedir_cap, vaddr, region->permissions, region->attributes);

    if (err) {
        printf ("_page_map: failed to map page: %s\n", seL4_Error_Message (err));
        return false;
    }

    return true;
}

/*
 * CALLER should double check that the vaddr is in a valid region
 */
frameidx_t
page_map (addrspace_t as, struct as_region* region, vaddr_t vaddr) {
    assert (as != NULL);

    pagetable_t pt = as->pagetable;
    assert (pt != NULL);

    int l1 = L1_IDX (vaddr);
    struct pt_table* table = pt->entries[l1];
    if (!table) {
        seL4_Word pt_cap;

        //printf ("page_map: mallocing new PT in page dir at idx %d\n", l1);
        table = malloc (sizeof (struct pt_table));
        if (!table) {
            printf ("page_map: malloc failed\n");
            return 0;
        }

        memset (table, 0, sizeof (struct pt_table));

        /* now create the capability */
        pt_cap = _pagetable_map (as, region->attributes, vaddr);
        if (!pt_cap) {
            free (table);
            return 0;
        }

        pt->table_caps[l1] = pt_cap;
        pt->entries   [l1] = table;
    }

    int l2 = L2_IDX (vaddr);
    struct pt_entry* entry = &(table->entries[l2]);

    if (entry->flags & PAGE_ALLOCATED) {
        printf ("page_map: page at vaddr 0x%x already allocated!\n", vaddr);
        return 0;
    }

    /* ok now try to map in addrspace */
    frameidx_t frame = frame_alloc ();
    if (!frame) {
        printf ("page_map: no memory left\n");
        return 0;
    }

    //printf ("page_map: mapping @ vaddr 0x%x and frame idx 0x%x (L1 idx = 0x%x, L2 idx = 0x%x)\n", vaddr, frame, l1, l2);
    if (!_page_map (vaddr, frame, region, as)) {
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

void
page_free (pagetable_t pt, vaddr_t vaddr) {
    struct pt_entry* entry = page_fetch (pt, vaddr);
    if (!entry) {
        return;
    }

    if (!(entry->flags & PAGE_ALLOCATED)) {
        return;
    }

    entry->flags &= ~PAGE_ALLOCATED;
    frame_free (entry->frame_idx);
}

void
pagetable_free (pagetable_t pt) {
    /* walk level 1 */
    uint32_t l1 = 0;
    while (l1 < PAGETABLE_L1_SIZE) {
        struct pt_table* table = pt->entries[l1];
        if (table) {
            /* walk level 2 */
            /* and revoke initial cap - will destroy our copy but not the original */
        }

        l1++;
    }
}