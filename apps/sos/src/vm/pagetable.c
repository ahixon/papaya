#include <sel4/sel4.h>
#include <assert.h>
#include <string.h>

#include <cspace/cspace.h>

#include "ut_manager/ut.h"

#include "mapping.h"
#include "vmem_layout.h"
#include "frametable.h"

#include <vfs.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

#include "pagetable.h"

extern seL4_CPtr _badgemap_ep;      /* XXX: move later */

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


                vaddr_t vaddr = (i << PAGETABLE_L1_BITS) | (j << PAGETABLE_L2_BITS);
                printf ("\t0x%03x: vaddr %08x frame %p paddr %08x %s %s\n", j, vaddr, entry.frame,
                    entry.frame->paddr, entry.flags & PAGE_SHARED ? "SHARED" : "", entry.flags & PAGE_SWAPPING ? "SWAPPING" : "");
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

    /* we map in each page when the time comes (ie when we get a VM fault) */
    memset (pt, 0, sizeof (struct pt_directory));

    return pt;
}

static seL4_Word
pagetable_kernel_install_pt (addrspace_t as, seL4_ARM_VMAttributes attributes,
    vaddr_t vaddr, seL4_Word* pt_ret_addr) {

    int err;
    seL4_Word pt_cap, pt_addr;

    pt_addr = ut_alloc (seL4_PageTableBits);
    if (pt_addr == 0){
        printf ("pagetable_kernel_install_pt: utalloc failed\n");
        return 0;
    }

    /* Create the frame cap */
    err = cspace_ut_retype_addr(pt_addr, 
                                 seL4_ARM_PageTableObject, seL4_PageTableBits,
                                 cur_cspace, &pt_cap);
    if (err) {
        printf ("%s: retype failed: %s\n",__FUNCTION__,
            seL4_Error_Message (err));

        ut_free (pt_addr, seL4_PageTableBits);
        return 0;
    }

    /* Tell seL4 to map the PT in for us */
    err = seL4_ARM_PageTable_Map(pt_cap, 
                                 as->pagedir_cap, 
                                 vaddr, 
                                 attributes);
    //printf ("mapped 0x%x => 0x%x\n", vaddr, pt_cap);

    if (err) {
        printf ("%s: seL4_ARM_PageTable_Map failed: %s\n",
            __FUNCTION__, seL4_Error_Message(err));

        //ut_free (pt_addr, seL4_PageTableBits);

        /* if someone's already mapped a pagetable in, don't worry about it */
        /* FIXME: remove once mapping.c is nixed */
        if (err != seL4_DeleteFirst) {
            return 0;
        }

        printf ("ignoring, since it was just a DeleteFirst...\n");
    }

    *pt_ret_addr = pt_addr;
    return pt_cap;
}

/* 
 * Maps the cap associated with a PTE into the kernel, using the region's
 * permission and attributes.
 * 
 * Returns true on success, false otherwise.
*/
int
pagetable_kernel_map_page (struct pt_entry* pte, vaddr_t vaddr,
    struct as_region* region, addrspace_t as) {

    assert (pte->cap);

    int err = seL4_ARM_Page_Map (pte->cap, as->pagedir_cap,
        vaddr, region->permissions, region->attributes);

    if (err) {
        printf ("pagetable_kernel_map_page: failed to map page: %s\n",
            seL4_Error_Message (err));
    }

    return !err;
}

extern thread_t current_thread;

/* Fetches the PTE from the pagetable assocated with a virtual address. 
 * If it does not exist, it is created.
 *
 * Returns NULL on failure.

 FIXME: this function should be renamed vs page_fetch
 */
struct pt_entry* 
page_fetch_entry (addrspace_t as, seL4_ARM_VMAttributes attributes,
    pagetable_t pt, vaddr_t vaddr) {

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
        pt->table_addrs[l1] = pt_addr;
    }

    int l2 = L2_IDX (vaddr);
    struct pt_entry* entry = &(table->entries[l2]);

    return entry;
}

/*
 * Allocates an underlying frame if required, and maps the page to that frame.
 *
 * Returns NULL if not page could be guaranteed. However, it might be assigned
 * in the near future. You should consult the "status" flag, which is set as 
 * follows:
 *  - PAGE_FAILED   Could not map page. either internal OOM error, or
 *                  invalid arguments.
 *
 *  - PAGE_SWAP_IN  Page is being swapped in - either from disk, or mmaped I/O.
 *                  Provided callback will be called with given event on success
 *                  
 *                  NOTE: this should not happen on device syscalls (since they
 *                  are backed by physical memory already), and you may
 *                  wish to not handle this case for booting the system, since
 *                  it's fair enough to assume that you'd have enough memory to
 *                  boot the system. In that case, you may wish to make the
 *                  callback panic the system.
 *
 *  - PAGE_SWAP_OUT Page is being swapped out to swapfile. You should reattempt
 *                  to map the page when your callback is called.
 */
struct pt_entry*
page_map (addrspace_t as, struct as_region *region, vaddr_t vaddr, int *status,
    void *cb, struct pawpaw_event* evt) {

    int err;

    assert (as != NULL);
    assert (region != NULL);
    assert (status);

    pagetable_t pt = as->pagetable;
    assert (pt != NULL);

    struct pt_entry* entry = page_fetch_entry (
        as, region->attributes, pt, vaddr);

    if (!entry) {
        printf ("%s: fetching PTE failed\n", __FUNCTION__);
        *status = PAGE_FAILED;
        return NULL;
    }

    if (entry->flags & PAGE_ALLOCATED) {
        /* page already allocated.
         *
         * we COULD return entry->frame but we want to return NULL since
         * the we really shouldn't be calling page_map on the same address twice
         * and this indicates a bug, so if we return an error we can more easily
         * see who the calling function was. */
        printf ("%s: page already allocated (that's OK, but you probably have "
            "a bug elsewhere)\n", __FUNCTION__);
        *status = PAGE_FAILED;
        return NULL;
    }

    if (entry->flags & PAGE_SWAPPING) {
        /* page being swapped at the moment, don't do anything */
        printf ("%s: page still being swapped, not remapping\n", __FUNCTION__);
        *status = PAGE_SWAPPING;
        return NULL;
    }

    // printf ("mapping 0x%x\n", vaddr);

    if (!entry->frame) {
        // printf ("0x%x had no existing frame information allocated\n", vaddr);
        entry->frame = frame_new_from_untyped (0);
        if (!entry->frame) {
            printf ("%s: failed to create new frame\n", __FUNCTION__);
            *status = PAGE_FAILED;
            return NULL;
        }
    }

    /* allocate frame if not already provided */
    if (!entry->frame->paddr) {

        assert (!(entry->flags & PAGE_SWAPPING));
        /*if (entry->flags & PAGE_SWAPPING) {
            printf ("%s: cannot allocate a page while it's being swapped in/out\n");
            *status = PAGE_FAILED;
            return NULL;
        }*/

        // printf ("allocating physical mem for vaddr 0x%x\n", vaddr);
        seL4_Word untyped = ut_alloc (seL4_PageBits);
        if (!untyped) {
            /* TODO: swapping goes here */
            //*status = SWAP_OUT;
            printf ("page_map: no physical memory left - should swap\n");
            return NULL;
        }
        
        entry->frame = frame_alloc_from_untyped (entry->frame, untyped);
    } else {
        // printf ("0x%x already had physical mem\n", entry->frame->paddr);
    }
    
    if (!entry->cap) {
        err = cspace_ut_retype_addr(entry->frame->paddr,
                                     seL4_ARM_SmallPageObject, seL4_PageBits,
                                     cur_cspace, &(entry->cap));
        if (err != seL4_NoError) {
            printf ("page_map: could not retype: %s\n",
                seL4_Error_Message (err));
            /* FIXME: only free if we did the allocation, otherwise leave for
             * whoever */
            frame_free (entry->frame);
            entry->frame = NULL;

            *status = PAGE_FAILED;
            return NULL;
        }
    } else {
        //printf ("already had cap\n");
        /* FIXME: is this correct? */
    }

    if (!pagetable_kernel_map_page (entry, vaddr, region, as)) {
        printf ("actual page map failed\n");
        /* FIXME: only free if we did the allocation, otherwise leave for
         * whoever */
        frame_free (entry->frame);
        entry->frame = NULL;

        *status = PAGE_FAILED;
        return NULL;
    }

    if (entry->frame->file) {
        entry->flags |= PAGE_SWAPPING;

        printf ("%s: swapping in page\n", __FUNCTION__);
        if (mmap_swap (PAGE_SWAP_IN, vaddr, entry, cb, evt)) {
            *status = PAGE_SWAP_IN;
        } else {
            *status = PAGE_FAILED;
        }

        return NULL;
    } else {
        // printf ("was not file backed\n");
    }

    entry->flags |= PAGE_ALLOCATED;
    *status = PAGE_SUCCESS;
    return entry;
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
        printf ("%s: failed - no page table @ L1 idx 0x%x\n", __FUNCTION__, l1);
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
                page_unmap (entry);
            }

            /* now free the table cap + addrs */
            if (pt->table_caps [l1]) {
                if (seL4_ARM_PageTable_Unmap (pt->table_caps [l1])) {
                    printf ("%s: unmap failed\n", __FUNCTION__);
                    /* FIXME: continue depending on error type since delete/free
                    would be bad */
                }

                cspace_delete_cap (cur_cspace, pt->table_caps [l1]);
            }

            if (pt->table_addrs[l1]) {
                ut_free (pt->table_addrs[l1], seL4_PageTableBits);
            }

            free (table);
        }
    }

    /* free page dir */
    free (pt);
}

int
page_unmap (struct pt_entry* entry) {
    if (entry && entry->flags & PAGE_ALLOCATED) {
        entry->flags &= ~PAGE_ALLOCATED;

        /* unmap + delete the cap to the page */
        if (entry->cap) {
            seL4_ARM_Page_Unmap (entry->cap);
            //seL4_ARM_Page_FlushCaches(entry->cap);
            cspace_revoke_cap (cur_cspace, entry->cap); /* FIXME: hmm */
            cspace_delete_cap (cur_cspace, entry->cap);


            entry->cap = 0;
        }

        /* "free" the frame - removes from refcount and only
         * actually releases underlying frame when it's zero. */
        if (entry->frame) {
            frame_free (entry->frame);
            entry->frame = NULL;
        }

        return true;
    } else {
        return false;
    }
}

/*swapp
 * Maps in a new page at a given virtual address in one address space,
 * sharing the underlying frame from another page in another address space.
 */
struct pt_entry*
page_map_shared (addrspace_t as_dst, struct as_region* reg_dst, vaddr_t dst,
    addrspace_t as_src, struct as_region* reg_src, vaddr_t src, int cow) {

    struct pt_entry* src_entry = page_fetch_entry (
        as_src, reg_src->attributes, as_src->pagetable, src);

    struct pt_entry* dst_entry = page_fetch_entry (
        as_dst, reg_dst->attributes, as_dst->pagetable, dst);

    if (!src_entry || !dst_entry) {
        printf ("%s: missing source or dest PTE\n", __FUNCTION__);
        return NULL;
    }

    /* mark as shared */
    src_entry->flags |= PAGE_SHARED;
    if (cow) {
        src_entry->flags |= PAGE_COPY_ON_WRITE;
        /* FIXME: remap src + dest pages as read-only */
    }

    /* FIXME: ensure swap in only, NOT swap out */
    if (!(src_entry->flags & PAGE_ALLOCATED || src_entry->flags & PAGE_SWAPPING) || !src_entry->frame) {
        /* FIXME: pass down swapping requirements down chain here */
        printf ("mapping shared page, should check for swapping\n");
        //page_map (as_src, reg_src, src);
        int status = PAGE_FAILED;
        struct pt_entry* page = page_map (as_src, reg_src, src, &status, NULL, NULL);
        /* FIXME: should be able to handle swap outs!!! */
        assert (status == PAGE_SUCCESS);
        assert (page);
    }

    assert (src_entry->frame);

    /* make them share flags and frames */
    dst_entry->frame = src_entry->frame;
    dst_entry->flags = src_entry->flags;

    /* update underlying frame refcount */
    struct frameinfo* frame = dst_entry->frame;
    unsigned int refcount = frame_get_refcount (frame);
    printf ("refcount for paddr 0x%x was: %u\n", frame->paddr, refcount);
    frame_set_refcount (frame, refcount + 1);
    printf ("refcount now: %u\n", frame_get_refcount (frame));

    /* duplicate the kernel's page table entry */
    dst_entry->cap = cspace_copy_cap (
        cur_cspace, cur_cspace, src_entry->cap, seL4_AllRights);

    if (!dst_entry->cap) {
        printf ("%s: copy cap for shared page failed\n", __FUNCTION__);
        page_unmap (dst_entry);
        return NULL;
    }

    /* and map that new PTE cap into the dest address space */
    if (!pagetable_kernel_map_page (dst_entry, dst, reg_dst, as_dst)) {
        printf ("%s: PTE kernel map failed\n", __FUNCTION__);
        page_unmap (dst_entry);
        return NULL;
    }

    return dst_entry;
}
