#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <stdio.h>

#include <pawpaw.h>
#include <vm/vmem_layout.h>
#include <uid.h>

#include <vm/addrspace.h>

#include <badgemap.h>

extern seL4_CPtr _badgemap_ep;
extern short badgemap_found;
extern thread_t current_thread;

static
struct as_region* share_alloc_region (void) {
    struct as_region* share_reg = as_define_region_within_range (current_thread->as,
            PROCESS_SHARE_START, PROCESS_SHARE_END, PAGE_SIZE, seL4_AllRights, REGION_SHARE);

    if (!share_reg) {
        /* pick a share region based on ??? and unmap page + clear contents - DO NOT reduce refcount (only for unmount) */
        printf ("%s: ran out of regions, should normally swap but not implemented\n", __FUNCTION__);
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
    seL4_Word ep_cpy = cspace_copy_cap (cur_cspace, current_thread->croot, seL4_GetMR (1), seL4_AllRights);

    seL4_MessageInfo_t local_msg = seL4_MessageInfo_new (0, 0, 0, 0);
    seL4_Call (ep_cpy, local_msg);  /* FIXME: this is bad - should do notify and get callback since this can be an arbitary EP */

    if (!badgemap_found) {
        printf ("%s: badgemapper returned failure\n", __FUNCTION__);
        /* didn't actually call mapper service */
        return PAWPAW_EVENT_UNHANDLED;
    }

    seL4_Word id = seL4_GetMR (2);
    thread_t src_thread = thread_lookup (seL4_GetMR (0));
    if (!src_thread) {
        printf ("%s: fetching source thread failed\n", __FUNCTION__);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);

        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* ensure the other region still exists */
    struct as_region* other_reg = as_get_region_by_addr (src_thread->as, seL4_GetMR (1));
    if (!other_reg) {
        printf ("%s: fetching source region failed\n", __FUNCTION__);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);

        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* make a new region to place it in */
    struct as_region* share_reg = share_alloc_region ();
    if (!share_reg) {
        printf ("%s: alloc region failed\n", __FUNCTION__);
        return PAWPAW_EVENT_UNHANDLED;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, id);
    seL4_SetMR (1, share_reg->vbase);
    seL4_SetMR (2, 0);

    /* cool, now shared map the two */
    struct pt_entry* pte = page_map_shared (current_thread->as, share_reg, share_reg->vbase,
        src_thread->as, other_reg, other_reg->vbase, false);

    if (!pte) {
        printf ("%s: map shared failed\n", __FUNCTION__);
        return PAWPAW_EVENT_UNHANDLED;
    }

    return PAWPAW_EVENT_NEEDS_REPLY;
}

#if 0
seL4_MessageInfo_t syscall_sbuf_mount (thread_t thread) {
    seL4_MessageInfo_t failure = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 3);

    /* my copy - FIXME: should ahve a copy when we make it and not do this??? */
    seL4_Word ep_cpy = cspace_copy_cap (cur_cspace, thread->croot, seL4_GetMR (1), seL4_AllRights);

    seL4_MessageInfo_t local_msg = seL4_MessageInfo_new (0, 0, 0, 0);
    seL4_Call (ep_cpy, local_msg);

    if (badgemap_found) {
        seL4_Word id = seL4_GetMR (2);
        /* FIXME: ensure thread still exists */
        thread_t src_thread = thread_lookup (seL4_GetMR (0));
        printf ("WANT TO SHARE MEMORY WITH %s and %s\n", thread->name, src_thread->name);

        //printf ("start addr was 0x%x\n", seL4_GetMR (1));
        struct as_region* other_reg = as_get_region_by_addr (src_thread->as, seL4_GetMR (1));
        //printf ("had other reg %p\n", other_reg);

        /* FIXME: factorise out from above and here */
        vaddr_t reg_start;
        vaddr_t reg_offset = PROCESS_BEANS;

        /* check if there was an exisiting region, and if so, increase our offset so that we create another region directly following it */
        struct as_region* reg = as_get_region_by_type (thread->as, REGION_BEANS);
        if (reg) {
            reg_offset = reg->vbase + reg->size;
        }

        /* create a bean region */
        reg = as_define_region (thread->as, reg_offset, other_reg->size, seL4_AllRights, REGION_BEANS);

        if (!reg) {
            /* FIXME: return some failure status */
            seL4_SetMR (0, 0);
            return failure;
        }

        reg_start = reg->vbase;

        /* finally link them. */
        printf ("linking 0x%x and 0x%x (%s)\n", other_reg->vbase, reg->vbase, thread->name);
        if (!as_region_link (other_reg, reg)) {
            seL4_SetMR (0, 0);
            return failure;
        }

        //printf ("giving back 0x%x (size 0x%x)\n", reg_start, reg->size);
        seL4_SetMR (0, reg_start);
        seL4_SetMR (1, reg->size);
        seL4_SetMR (2, id);

        printf ("++++ SBUF FOR %s HAS ID %d\n", thread->name, id);
        
        return reply;
    }

    seL4_SetMR (0, 0);
    return failure;
}
#endif
