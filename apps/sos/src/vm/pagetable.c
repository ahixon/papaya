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
pagetable_kernel_install_pt (addrspace_t as, seL4_ARM_VMAttributes attributes, vaddr_t vaddr, seL4_Word* pt_ret_addr) {
    int err;
    seL4_Word pt_cap, pt_addr;

    pt_addr = ut_alloc(seL4_PageTableBits);
    if (pt_addr == 0){
        printf ("pagetable_kernel_install_pt: utalloc failed\n");
        return 0;
    }

    /* Create the frame cap */
    err = cspace_ut_retype_addr(pt_addr, 
                                 seL4_ARM_PageTableObject, seL4_PageTableBits,
                                 cur_cspace, &pt_cap);
    if (err) {
        printf ("pagetable_kernel_install_pt: retype failed: %s\n", seL4_Error_Message (err));
        ut_free (pt_addr, seL4_PageTableBits);
        return 0;
    }

    /* Tell seL4 to map the PT in for us */
    err = seL4_ARM_PageTable_Map(pt_cap, 
                                 as->pagedir_cap, 
                                 vaddr, 
                                 attributes);

    if (err) {
        printf ("pagetable_kernel_install_pt: seL4_ARMpagetable_kernel_install_pt failed: %s\n", seL4_Error_Message(err));
        ut_free (pt_addr, seL4_PageTableBits);

        /* if someone's already mapped a pagetable in, don't worry about it */
        /* FIXME: remove once mapping.c is nixed */
        if (err != seL4_DeleteFirst) {
            return 0;
        }
    }

    *pt_ret_addr = pt_addr;
    return pt_cap;
}

/* 
 * Maps a the page already created from the PTE (use page_map) into the kernel, using the region's
 * permission and attributes.
 * 
 * Returns CPtr to page capability on success, 0 otherwise.
*/
int
pagetable_kernel_map_page (vaddr_t vaddr, frameidx_t frame, struct as_region* region, addrspace_t as) {
    int err;
    seL4_Word frame_cap, dest_cap;

    frame_cap = frametable_fetch_cap (frame);
    assert (frame_cap);

    dest_cap = cspace_copy_cap (cur_cspace, cur_cspace, frame_cap, seL4_AllRights);
    assert (dest_cap);

    //printf ("pagetable_kernel_map_page: mapping cap (originally 0x%x, copy is 0x%x) to vaddr 0x%x\n", frame_cap, dest_cap, vaddr);
    //printf ("           perms = %d, attrib = %d, pagedir cap = 0x%x\n", region->permissions, region->attributes, as->pagedir_cap);
    err = seL4_ARM_Page_Map(dest_cap, as->pagedir_cap, vaddr, region->permissions, region->attributes);

    if (err) {
        printf ("pagetable_kernel_map_page: failed to map page: %s\n", seL4_Error_Message (err));
        return 0;
    }

    return dest_cap;
}

/* Fetches the PTE from the pagetable assocated with a virtual address. 
 * If it does not exist, it is created.
 *
 * Returns NULL on failure.
 */
struct pt_entry* 
page_fetch_entry (addrspace_t as, seL4_ARM_VMAttributes attributes, pagetable_t pt, vaddr_t vaddr) {
    int l1 = L1_IDX (vaddr);
    struct pt_table* table = pt->entries[l1];
    if (!table) {
        seL4_Word pt_cap, pt_addr;

        table = malloc (sizeof (struct pt_table));
        if (!table) {
            printf ("page_map: malloc failed\n");
            return NULL;
        }

        memset (table, 0, sizeof (struct pt_table));

        /* now create the capability */
        pt_cap = pagetable_kernel_install_pt (as, attributes, vaddr, &pt_addr);
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
 * Allocates an underlying frame, and maps the page to that frame.
 * Should only be called once.
 */
frameidx_t
page_map (addrspace_t as, struct as_region* region, vaddr_t vaddr) {
    assert (as != NULL);
    assert (region != NULL);

    pagetable_t pt = as->pagetable;
    assert (pt != NULL);

    struct pt_entry* entry = page_fetch_entry (as, region->attributes, pt, vaddr);
    if (!entry) {
        return 0;
    }

    if (entry->flags & PAGE_ALLOCATED) {
        /* page already allocated.
         *
         * we COULD return entry->frame_idx but we want to return NULL since
         * the we really shouldn't be calling page_map on the same address twice
         * and this indicates a bug, so if we return an error we can more easily
         * see who the calling function was. */
        return 0;
    }

    /* ok now try to map in addrspace */
    frameidx_t frame = frame_alloc ();
    if (!frame) {
        printf ("page_map: no memory left\n");
        return 0;
    }
    
    entry->cap = pagetable_kernel_map_page (vaddr, frame, region, as);
    if (!entry->cap) {
        printf ("actual page map failed\n");
        frame_free (frame);
        return 0;
    }

    entry->frame_idx = frame;
    entry->flags |= PAGE_ALLOCATED;

    return frame;
}

/*
 * Fetches the PTE associated with a given virtual address.
 *
 * Returns NULL if no such PTE has been created yet.
 */
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

/* Frees a pagetable, and all associated frames and CNodes. */
void
pagetable_free (pagetable_t pt) {
    for (uint32_t l1 = 0; l1 < PAGETABLE_L1_SIZE; l1++) {
        struct pt_table* table = pt->entries[l1];
        if (table) {
            for (uint32_t l2 = 0; l2 < PAGETABLE_L2_SIZE; l2++) {
                struct pt_entry* entry = &(table->entries[l2]);

                if (entry->flags & PAGE_ALLOCATED) {
                    entry->flags &= ~PAGE_ALLOCATED;

                    /* unmap + delete the cap to the page */
                    assert (entry->cap);
                    seL4_ARM_Page_Unmap (entry->cap);   /* FIXME: do we need this? or does it break? */
                    cspace_delete_cap (cur_cspace, entry->cap);

                    /* "free" the frame - removes from refcount and only
                     * actually releases underlying frame when it's zero. */
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

/*
 * Maps in a new page at a given virtual address in one address space,
 * sharing the underlying frame from another page in another address space.
 */
struct pt_entry*
page_map_shared (addrspace_t as_dst, struct as_region* reg_dst, vaddr_t dst,
    addrspace_t as_src, struct as_region* reg_src, vaddr_t src, int cow) {

    struct pt_entry* src_entry = page_fetch_entry (as_src, reg_dst->attributes, as_src->pagetable, src);
    struct pt_entry* dst_entry = page_fetch_entry (as_dst, reg_dst->attributes, as_dst->pagetable, dst);

    if (!src_entry || !dst_entry) {
        return NULL;
    }

    /* mark as shared */
    src_entry->flags |= PAGE_SHARED;
    if (cow) {
        src_entry->flags |= PAGE_COPY_ON_WRITE;
        /* FIXME: remap src + dest pages as read-only */
    }

    if (!(src_entry->flags & PAGE_ALLOCATED)) {
        printf ("%s: warning! source page wasn't allocated yet - allocating\n", __FUNCTION__);
        page_map (as_src, reg_src, src);
    }

    /* make them share flags and frames */
    dst_entry->frame_idx = src_entry->frame_idx;
    dst_entry->flags = src_entry->flags;

    /* update underlying frame refcount */
    struct frameinfo* frame = frametable_get_frame (dst_entry->frame_idx);
    frame_set_refcount (frame, frame_get_refcount (frame) + 1);

    /* map the dest page into the dest address space */
    dst_entry->cap = pagetable_kernel_map_page (dst, dst_entry->frame_idx, reg_dst, as_dst);
    if (!dst_entry->cap) {
        return NULL;
    }

    return dst_entry;
}
