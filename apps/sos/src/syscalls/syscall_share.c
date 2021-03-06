#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <stdio.h>

#include <pawpaw.h>
#include <vm/vmem_layout.h>
#include <uid.h>

#include <vm/addrspace.h>
#include <vm/frametable.h>
#include <thread.h>

#include <services/services.h>
#include <assert.h>

extern seL4_CPtr _badgemap_ep;
extern short badgemap_found;
extern thread_t current_thread;

static
struct as_region* share_alloc_region (void) {
    struct as_region* share_reg = as_define_region_within_range (current_thread->as,
            PROCESS_SHARE_START, PROCESS_SHARE_END, PAGE_SIZE, seL4_AllRights, REGION_SHARE);

    if (!share_reg) {
        /* pick a share region based on ??? and unmap page + clear contents - DO NOT reduce refcount (only for unmount) */
        //printf ("%s: ran out of regions, should normally swap but not implemented\n", __FUNCTION__);
        return NULL;
    }

    return share_reg;
}

/*
 * creates a new memory share, and places a cap to it
 * in the process' address space. 
 *
 * reply contains:
 *  - CPtr to cap in thread's cspace. it can give this to other threads
 *    so they can MOUNT/UNMOUNT them, thus being able to read/write shared
 *    memory. 0 if allocation failed. Note that if there is no more space in
 *    the SHARE region, then an existing share will be swapped out and it's ID
 *    placed in the last message register. It will be chosen based on ???
 *    NOTE that this does not reduce it's refcount - if it is no longer being
 *    used you must UNMOUNT it.
 *
 *  - unique global share ID to refer to this share.
 *  - vaddr in addrspace in which thread should use to access this share.
 *  - ID of swapped out share. 0 if none.
 */
int syscall_share_create (struct pawpaw_event* evt) {
    struct as_region* share_reg = share_alloc_region ();
    if (!share_reg) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    //printf ("cool created region at 0x%x\n", share_reg->vbase);

	evt->reply = seL4_MessageInfo_new (0, 0, 0, 4);

    /* badge with unique ID */
    seL4_Word id = cid_next ();
    maps_append (id, current_thread->pid, share_reg->vbase);	/* NOTE: not thread safe! */
    seL4_CPtr their_cbox_cap = cspace_mint_cap (current_thread->croot, cur_cspace,
        _badgemap_ep, seL4_AllRights,
        seL4_CapData_Badge_new (id));

    seL4_SetMR (0, their_cbox_cap);
    seL4_SetMR (1, id);
    seL4_SetMR (2, share_reg->vbase);
    seL4_SetMR (3, 0);

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_share_mount (struct pawpaw_event* evt) {
    /* lookup provided CNode */
    seL4_Word ep_cpy = cspace_copy_cap (cur_cspace, current_thread->croot, evt->args[0], seL4_AllRights);
    if (!ep_cpy) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    seL4_MessageInfo_t local_msg = seL4_MessageInfo_new (0, 0, 0, 0);
    /* FIXME: this is VERY BAD - should do notify and get callback since this can be an arbitary EP and we may never get a callback */
    seL4_Call (ep_cpy, local_msg);

    cspace_delete_cap (cur_cspace, ep_cpy);

    seL4_Word id = seL4_GetMR (2);
    seL4_Word thread_id = seL4_GetMR (0); 

    /* FIXME: maybe made ids start from 1 not 0 */
    if (!badgemap_found) {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);

        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    thread_t src_thread = thread_lookup (thread_id);
    if (!src_thread) {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);

        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* ensure the other region still exists */
    seL4_Word src_vaddr = seL4_GetMR (1);
    struct as_region* other_reg = as_get_region_by_addr (src_thread->as, src_vaddr);
    if (!other_reg) {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);

        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* make a new region to place it in */
    struct as_region* share_reg = share_alloc_region ();
    if (!share_reg) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, id);
    seL4_SetMR (1, share_reg->vbase);
    seL4_SetMR (2, 0);

    /* FIXME: handle swapping */
    int status = PAGE_FAILED;
    struct pt_entry* pte = page_map_shared (current_thread->as, share_reg, share_reg->vbase,
        src_thread->as, other_reg, src_vaddr, false, &status, NULL, NULL);
    assert (status == PAGE_SUCCESS);

    /* pin if required */
    //if (src_thread->pinned || current_thread->pinned) {
        pte->frame->flags |= FRAME_PINNED;
        // TODO: don't always pin shares (this is probably safe to remove)
    //}

    if (!pte) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_share_unmount (struct pawpaw_event* evt) {
    /* TODO: if refcounting share cap, use evt->args[0] */
    cspace_delete_cap (current_thread->croot, evt->args[0]);
    
    struct as_region* reg = as_get_region_by_addr (current_thread->as, evt->args[1]);
    if (!reg) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    /* unmap the associated page - FIXME: what is "abstraction" hurrr */
    struct pt_entry* pte = page_fetch_existing (current_thread->as->pagetable, reg->vbase);
    if (!pte) {
        return PAWPAW_EVENT_UNHANDLED;
    }
    
    int success = page_free (pte);
    seL4_SetMR (0, success);
    as_region_destroy (current_thread->as, reg);

    return PAWPAW_EVENT_NEEDS_REPLY;
}

struct as_region* create_share_reg (seL4_CPtr *cap, seL4_Word *dest_id, int map) {
    struct as_region* share_reg = as_define_region_within_range (cur_addrspace,
            PROCESS_SCRATCH_START, PROCESS_SCRATCH_END, PAGE_SIZE, seL4_AllRights, REGION_SHARE);

    assert (share_reg);

    /* map straight away */
    if (map) {
        int status = PAGE_FAILED;
        assert (page_map (cur_addrspace, share_reg, share_reg->vbase, &status, NULL, NULL));
        assert (status != PAGE_FAILED);
    }

    /* badge with unique ID */
    seL4_Word id = cid_next ();
    maps_append (id, 0, share_reg->vbase);
    seL4_CPtr their_cbox_cap = cspace_mint_cap (cur_cspace, cur_cspace,
        _badgemap_ep, seL4_AllRights,
        seL4_CapData_Badge_new (id));

    *cap = their_cbox_cap;
    *dest_id = id;
    return share_reg;
}