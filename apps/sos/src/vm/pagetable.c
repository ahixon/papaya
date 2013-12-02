#include <sel4/sel4.h>
#include <assert.h>
#include <string.h>

#include <stdio.h>
#include <stdlib.h>

#include <cspace/cspace.h>

#include "ut_manager/ut.h"

#include "mapping.h"
#include "vmem_layout.h"
#include "frametable.h"
#include "syscalls/syscall_table.h"
#include "services/services.h"

#include <vfs.h>

#include "pagetable.h"

extern seL4_CPtr _badgemap_ep;      /* XXX: move later */

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
        return 0;
    }

    /* Create the frame cap */
    err = cspace_ut_retype_addr(pt_addr, 
                                 seL4_ARM_PageTableObject, seL4_PageTableBits,
                                 cur_cspace, &pt_cap);
    if (err) {
        ut_free (pt_addr, seL4_PageTableBits);
        return 0;
    }

    /* Tell seL4 to map the PT in for us */
    err = seL4_ARM_PageTable_Map(pt_cap, 
                                 as->pagedir_cap, 
                                 vaddr, 
                                 attributes);

    if (err) {
        /* functions in nmapping.c may have already mapped a PT for us; in that
         * case, we don't actually have an error. TODO: Would be nice to 
         * replace that with our nice region code though */
        if (err != seL4_DeleteFirst) {
            ut_free (pt_addr, seL4_PageTableBits);
            return 0;
        }

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

    return !err;
}

extern thread_t current_thread;

/* Fetches the PTE from the pagetable assocated with a virtual address. 
 * If it does not exist, it is created.
 *
 * Returns NULL on failure.
 */
struct pt_entry* 
page_fetch_new (addrspace_t as, seL4_ARM_VMAttributes attributes,
    pagetable_t pt, vaddr_t vaddr) {

    int l1 = L1_IDX (vaddr);
    struct pt_table* table = pt->entries[l1];
    if (!table) {
        seL4_Word pt_cap, pt_addr;

        table = malloc (sizeof (struct pt_table));
        if (!table) {
            return NULL;
        }

        memset (table, 0, sizeof (struct pt_table));

        /* now create the capability */
        pt_cap = pagetable_kernel_install_pt (as, attributes, vaddr, &pt_addr);
        if (!pt_cap) {
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
 * Fetches the PTE associated with a given virtual address.
 *
 * Returns NULL if no such PTE has been created yet.
 */
struct pt_entry*
page_fetch_existing (pagetable_t pt, vaddr_t vaddr) {
    uint32_t l1 = L1_IDX(vaddr);
    uint32_t l2 = L2_IDX(vaddr);

    struct pt_table* table = pt->entries[l1];
    if (!table) {
        return NULL;
    }

    struct pt_entry* entry = &(table->entries[l2]);
    return entry;
}

/**
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
 *                  callback panic the system, or provide a NULL callback.
 *
 *  - PAGE_SWAP_OUT Page is being swapped out to swapfile. You should reattempt
 *                  to map the page when your callback is called.
 *
 * Yes, this is a long function, but it [was] pretty readable :)
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

    /* get the PTE from the pagetable, creating any L1 and L2 pagetables
     * as required */
    struct pt_entry* entry = page_fetch_new (
        as, region->attributes, pt, vaddr);

    if (!entry) {
        *status = PAGE_FAILED;
        return NULL;
    }

    /* check if page already allocated - ie has cap AND underyling frame */
    if (entry->cap && entry->frame && entry->frame->flags & FRAME_FRAMETABLE) {
        return entry;
    }

    if (entry->frame && entry->frame->flags & FRAME_SWAPPING) {
        /* underlying frame being swapped at the moment - caller should wait
         * until callback from original page is called, then try again */
        /* FIXME: should have more specific flag here */
        *status = PAGE_SWAP_OUT;
        return NULL;
    }

    /* no frame yet, or had frame but no physical address */
    if (!entry->frame || !entry->frame->paddr) {
        if (entry->frame) {
            /* mmap'd page, with no physical memory backing yet - move into
             * frametable if we can alloc a frame */
            entry->frame = frame_alloc_from_existing (entry->frame);
        } else {
            /* just a regular page, just alloc a frame normally */
            entry->frame = frame_alloc ();

            if (!entry->frame && as->pinned) {
                /* use reserved pages if we're all out */
                paddr_t paddr = frame_get_reserved ();
                if (!paddr) {
                    /* well, we're screwed now, aren't we... */
                    *status = PAGE_FAILED;
                    return NULL;
                }

                entry->frame = frame_new_from_untyped (paddr);
                if (!entry->frame) {
                    /* well, we're screwed now, aren't we... */
                    *status = PAGE_FAILED;
                    return NULL;
                }

                entry->frame->flags |= FRAME_RESERVED;
            }
        }

        if (!entry->frame) {
            /* no frames left, pick one to replace it and swap out if req'd */
            struct frameinfo* target = frame_select_swap_target ();
            if (target->flags & FRAME_DIRTY) {
                /* only swap out if dirty */
                target->flags |= FRAME_SWAPPING;

                assert (target->page);

                /*
                 * Need to do this instead of unmapping all the pages, and then
                 * mapping the frame in again, because if it appears if frame
                 * refcount goes to zero, seL4 empties the page for us, and we
                 * lose all our data.
                 *
                 * seL4 BUG or undocumented feature?! ¯\(°_o)/¯ 
                 */
                seL4_CPtr target_copy;

                if (frame_get_refcount (target) > 1) {
                    /* one to many frame=>page mapping (shared page); walk the
                     * list of relevent pages and free those */

                    struct pagelist* pagenode = target->pages;
                    target_copy = cspace_copy_cap (cur_cspace, cur_cspace,
                        pagenode->page->cap, seL4_AllRights);
                    while (pagenode) {
                        if (!page_unmap (pagenode->page)) {
                            *status = PAGE_FAILED;
                            return NULL;
                        }

                        pagenode = pagenode->next;
                    }
                } else {
                    /* one to one frame=>page mapping */
                    target_copy = cspace_copy_cap (cur_cspace, cur_cspace,
                        target->page->cap, seL4_AllRights);

                    if (!page_unmap (target->page)) {
                        *status = PAGE_FAILED;
                        return NULL;
                    }
                }

                assert (target_copy);

                /* move the frame into the root server since vaddr is unknown */
                /* FIXME: need to clean/free after swap was successful */
                seL4_CPtr cap;
                seL4_Word dest_id;
                struct as_region* reg = create_share_reg(&cap, &dest_id, false);
                if (!reg) {
                    *status = PAGE_FAILED;
                    return NULL;
                }

                /* get the PTE for the newly created region */
                struct pt_entry* fake_page = page_fetch_new (cur_addrspace,
                    reg->attributes, cur_addrspace->pagetable, reg->vbase);

                assert (fake_page);

                /* and give it our frame */
                fake_page->cap = target_copy;
                fake_page->frame = target;
                target->page = fake_page;
                target->flags &= ~FRAME_PAGELIST;
                frame_set_refcount (target, 1);

                /* store old page pointer since we need to point all the old
                 * pages to the swapped out frame  XXX: yuck, use a struct! */
                evt->args[2] = (seL4_Word)target->page;    
                evt->args[3] = frame_get_refcount (target);

                /* schedule for swapping now that nobody has page mapped in */
                if (mmap_swap (PAGE_SWAP_OUT, reg->vbase, target, cb, evt)) {
                    *status = PAGE_SWAP_OUT;
                } else {
                    *status = PAGE_FAILED;
                }

                return NULL;
            } else {
                /* should be clean, so no need to write out data */
                /* FIXME: what if mmaped? */
                /* FIXME: need to move to non-frametable version, setup
                 * file ptr = , paddr should be 0 */

                /* create new virtual frame, copy refcount + flags */

                /* use page unmap code above */
                
                target->page = NULL;

                /* FIXME: need to zero page - involves mapping into sos */
                //memset (target->)

                entry->frame = target;
            }
        }
    }
    
    /* we should have a physical address to back the page now, so retype it to
       a page capability if we weren't already provided with one */
    if (!entry->cap) {
        err = cspace_ut_retype_addr (entry->frame->paddr,
                                     seL4_ARM_SmallPageObject, seL4_PageBits,
                                     cur_cspace, &(entry->cap));
        if (err != seL4_NoError) {
            //frame_free (entry->frame);
            entry->frame = NULL;

            *status = PAGE_FAILED;
            return NULL;
        }
    }

    /* map the page capability into the thread's VSpace */
    if (!pagetable_kernel_map_page (entry, vaddr, region, as)) {
        frame_free (entry->frame);
        //entry->frame = NULL;

        *status = PAGE_FAILED;
        return NULL;
    }

    /* if the frame was mmaped to a file, we're not quite done - thread
     * expects there to be data there, so swap in the contents, and notify
     * the root server when we're finished */
    if (entry->frame->file) {
        entry->frame->flags |= FRAME_SWAPPING;

        if (mmap_swap (PAGE_SWAP_IN, vaddr, entry->frame, cb, evt)) {
            *status = PAGE_SWAP_IN;
        } else {
            *status = PAGE_FAILED;
        }

        return NULL;
    }

    unsigned int refcount = frame_get_refcount (entry->frame);
    if (refcount == 1) {
        entry->frame->page = entry;
    } else if (refcount == 2) {
        struct pagelist* pagenode_old = malloc (sizeof (struct pagelist));
        struct pagelist* pagenode_new = malloc (sizeof (struct pagelist));

        if (!pagenode_new || !pagenode_old) {
            page_unmap (entry);

            *status = PAGE_FAILED;
            return NULL;
        }

        pagenode_old->next = NULL;
        pagenode_old->page = entry->frame->page;
        pagenode_new->next = pagenode_old;
        pagenode_new->page = entry;
        entry->frame->pages = pagenode_new;

        entry->frame->flags |= FRAME_PAGELIST;
    } else {
        struct pagelist* pagenode = malloc (sizeof (struct pagelist));
        if (!pagenode) {
            page_unmap (entry);

            *status = PAGE_FAILED;
            return NULL;
        }

        pagenode->page = entry;
        pagenode->next = entry->frame->pages;
        entry->frame->pages = pagenode;
    }

    *status = PAGE_SUCCESS;
    return entry;
}

/**
 * Frees a pagetable, and all associated pages and CNodes.
 */
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
                seL4_ARM_PageTable_Unmap (pt->table_caps [l1]);
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

/**
 * Unmaps and removes the cap for a given page.
 */
int
page_unmap (struct pt_entry* entry) {
    if (!entry) {
        return false;
    }

    /* unmap + delete the cap to the page */
    if (entry->cap) {
        //seL4_ARM_Page_FlushCaches(entry->cap);
        seL4_ARM_Page_Unmap (entry->cap);
        //cspace_revoke_cap (cur_cspace, entry->cap);
        cspace_delete_cap (cur_cspace, entry->cap);

        entry->cap = 0;
    }

    return true;
}

/**
 * Umaps, removes caps for and frees the underlying frame if it is not
 * being used by anyone else.
 */
int
page_free (struct pt_entry* entry) {
    page_unmap (entry);

    /* TODO: is this correct? */
    if (entry->frame) {
        /* remove ourselves from the pagelist TODO: move into frametable.c */
        if (entry->frame->flags & FRAME_PAGELIST) {
            struct pagelist* plentry = entry->frame->pages;
            struct pagelist* plprev = NULL;
            while (plentry) {
                struct pagelist* plnext = plentry->next;

                if (plentry->page == entry) {
                    if (plprev) {
                        plprev->next = plnext;
                    } else {
                        entry->frame->pages = plnext;
                    }

                    free (plentry);
                    break;
                }

                plprev = plentry;
                plentry = plnext;
            }
        }

        frame_free (entry->frame);
        entry->frame = NULL;
    }

    /* don't need to "free" page pointer per-se since it's part of
     * pagetable (L2) struct */

    return true;
}

/**
 * Maps in a new page at a given virtual address in one address space,
 * sharing the underlying frame from another page in another address space.
 *
 * This function definitely needs more parameters.... /s
 */
struct pt_entry*
page_map_shared (addrspace_t as_dst, struct as_region* reg_dst, vaddr_t dst,
    addrspace_t as_src, struct as_region* reg_src, vaddr_t src, int cow,
    int* status, void* cb, struct pawpaw_event* evt) {

    struct pt_entry* src_entry = page_fetch_new (
        as_src, reg_src->attributes, as_src->pagetable, src);

    struct pt_entry* dst_entry = page_fetch_new (
        as_dst, reg_dst->attributes, as_dst->pagetable, dst);

    if (!src_entry || !dst_entry) {
        return NULL;
    }

    if (!src_entry->frame || !src_entry) {
        src_entry = page_map (as_src, reg_src, src, status, cb, evt);

        if (!src_entry || *status != PAGE_SUCCESS) {
            /* if the page isn't immediately available, let the caller call us
             * again when swapping is done (or handle the error). */
            return NULL;
        }
    }

    assert (src_entry->frame);
    assert (src_entry->cap);

    /* mark as shared */
    if (cow) {
        /* FIXME: remap src + dest pages as read-only */
        src_entry->frame->flags |= FRAME_COPY_ON_WRITE;
    }
    
    /* make them share frames */
    dst_entry->frame = src_entry->frame;

    /* update underlying frame refcount */
    unsigned int refcount = frame_get_refcount (dst_entry->frame);
    frame_set_refcount (dst_entry->frame, ++refcount);

    /* duplicate the kernel's page table entry */
    dst_entry->cap = cspace_copy_cap (
        cur_cspace, cur_cspace, src_entry->cap, seL4_AllRights);

    if (!dst_entry->cap) {
        page_free (dst_entry);
        return NULL;
    }

    /* and map that new PTE cap into the dest address space */
    if (!pagetable_kernel_map_page (dst_entry, dst, reg_dst, as_dst)) {
        page_free (dst_entry);
        return NULL;
    }

    struct pt_entry* entry = dst_entry;

    /* update mapped list */
    assert (refcount >= 2);
    if (refcount == 2) {
        struct pagelist* pagenode_old = malloc (sizeof (struct pagelist));
        struct pagelist* pagenode_new = malloc (sizeof (struct pagelist));

        if (!pagenode_new || !pagenode_old) {
            page_unmap (entry);

            *status = PAGE_FAILED;
            return NULL;
        }

        pagenode_old->next = NULL;
        pagenode_old->page = entry->frame->page;
        pagenode_new->next = pagenode_old;
        pagenode_new->page = entry;
        entry->frame->pages = pagenode_new;

        entry->frame->flags |= FRAME_PAGELIST;
    } else {
        struct pagelist* pagenode = malloc (sizeof (struct pagelist));
        if (!pagenode) {
            page_unmap (entry);

            *status = PAGE_FAILED;
            return NULL;
        }

        pagenode->page = entry;
        pagenode->next = entry->frame->pages;
        entry->frame->pages = pagenode;
    }

    *status = PAGE_SUCCESS;
    return dst_entry;
}

void
pagetable_pin (pagetable_t pt) {
    if (!pt || !pt->entries) {
        return;
    }

    for (int i = 0; i < PAGETABLE_L1_SIZE; i++) {
        struct pt_table* table = pt->entries[i];
        if (table == NULL) {
            continue;
        }

        for (int j = 0; j < PAGETABLE_L2_SIZE; j++) {
            struct pt_entry* entry = &table->entries[j];

            if (entry->frame) {
                /* TODO: remove from frmaetable queue instead */
                entry->frame->flags |= FRAME_PINNED;
            }
        }
    }
}


void
pagetable_dump (pagetable_t pt) {
    printf ("== dumping pagetable %p\n", pt);
    for (int i = 0; i < PAGETABLE_L1_SIZE; i++) {
        struct pt_table* table = pt->entries[i];
        short printed = false;
        if (table == NULL) {
            continue;
        }

        printf ("0x%04x: ", i);

        for (int j = 0; j < PAGETABLE_L2_SIZE; j++) {
            struct pt_entry* entry = &table->entries[j];

            if (entry->frame) {
                if (printed) {
                    printf ("\t");
                } else {
                    printed = true;
                }


                vaddr_t vaddr =
                    (i << PAGETABLE_L1_BITS) | (j << PAGETABLE_L2_BITS);

                printf ("\t0x%03x: vaddr %08x PTE %p ", j, vaddr, entry);
                printf ("frame %p => paddr %08x cap 0x%08x %s %s\n",
                    entry->frame, entry->frame->paddr, entry->cap,
                    //entry.frame->flags & FRAME_SHARED ? "SHARED" : "",
                    "??",
                    //entry.frame->flags & FRAME_SWAPPING ? "SWAPPING" : "");
                    "??");
                    //entry.frame->flags & );
            }
        }

        if (!printed) {
            printf ("\n");
        }

    }
    printf ("\n");
}

/*
 * Prints an xxd-like representation of a given page.
 */
void  page_dump (struct pt_entry* page, vaddr_t vaddr) {
    assert (page->cap);
    seL4_CPtr cap = cspace_copy_cap (cur_cspace, cur_cspace, page->cap,
        seL4_AllRights);
    
    assert (cap);

    int err = map_page (cap, seL4_CapInitThreadPD,
        FRAMEWINDOW_VSTART, seL4_AllRights, seL4_ARM_Default_VMAttributes);

    assert (!err);

    for (int i = 0; i < PAGE_SIZE; i += 0x10) {
        printf ("%08x: ", vaddr + i);

        for (int j = 0; j < 0x10; j++) {
            char* addr = (char*)FRAMEWINDOW_VSTART + i + j;
            printf ("%02x", *addr);

            if ((seL4_Word)addr % 2) {
                printf (" ");
            }
        }

        printf (" ");

        /* and char rep */
        for (int j = 0; j < 0x10; j++) {
            char* addr = (char*)FRAMEWINDOW_VSTART + i + j;
            if (*addr >= 0x20 && *addr <= 0x7e) {
                printf ("%c", *addr);
            } else {
                printf (".");
            }
        }

        printf ("\n");
    }

    seL4_ARM_Page_Unmap (cap);
    cspace_delete_cap (cur_cspace, cap);
}