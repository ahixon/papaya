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

/*
 * sets aside a block of memory to share with other processes.
 * places a cap in their cspace that is a special endpoint that
 * they can send to other processes if they wish to use this sbuf.
 *
 * other processes merely need to send a MOUNT, and pass in the
 * cap.
 */
seL4_MessageInfo_t syscall_sbuf_create (thread_t thread) {
	seL4_MessageInfo_t failure = seL4_MessageInfo_new (0, 0, 0, 1);

	unsigned int size = seL4_GetMR (0);
	if (size == 0) {
		seL4_SetMR (0, 0);
		return failure;
	}

	seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 3);

	/* try to allocate the req'd number in the requestor's addrspace */
    vaddr_t reg_start;
    vaddr_t reg_offset = PROCESS_BEANS;

    /* check if there was an exisiting region, and if so, increase our offset so that we create another region directly following it */
    struct as_region* reg = as_get_region_by_type (thread->as, REGION_BEANS);
    if (reg) {
        reg_offset = reg->vbase + reg->size;
        printf ("reg offset now 0x%x\n", reg_offset);
    }

    /* create a bean region */
    reg = as_define_region (thread->as, reg_offset, PAGE_SIZE * size, seL4_AllRights, REGION_BEANS);

    if (!reg) {
        /* FIXME: return some failure status */
        seL4_SetMR (0, 0);
        return failure;
    }

    reg_start = reg->vbase;
    printf ("** created sbuf on %s, reg_start = 0x%x, SIZE = 0x%x\n", thread->name, reg_start, size);

    /* badge with unique ID */
    seL4_Word id = cid_next ();
    maps_append (id, thread->pid, reg_start);	/* NOTE: not thread safe! */
    seL4_CPtr their_cbox_cap = cspace_mint_cap(thread->croot, cur_cspace, _badgemap_ep, seL4_AllRights, seL4_CapData_Badge_new(id));
    printf ("their cap was %d\n", their_cbox_cap);

    /* and finally give it back! */
    seL4_SetMR (0, their_cbox_cap);
    seL4_SetMR (1, reg_start);
    seL4_SetMR (2, size);

    return reply;
}

seL4_MessageInfo_t syscall_sbuf_mount (thread_t thread) {
    seL4_MessageInfo_t failure = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 2);

    /* my copy - FIXME: should ahve a copy when we make it and not do this??? */
    seL4_Word ep_cpy = cspace_copy_cap (cur_cspace, thread->croot, seL4_GetMR (1), seL4_AllRights);

    seL4_MessageInfo_t local_msg = seL4_MessageInfo_new (0, 0, 0, 0);
    seL4_Call (ep_cpy, local_msg);

    if (badgemap_found) {
        /* FIXME: ensure thread still exists */
        thread_t src_thread = threadlist_lookup (seL4_GetMR (0));
        printf ("WANT TO SHARE MEMORY WITH %s and %s\n", thread->name, src_thread->name);

        printf ("start addr was 0x%x\n", seL4_GetMR (1));
        struct as_region* other_reg = as_get_region_by_addr (src_thread->as, seL4_GetMR (1));
        printf ("had other reg %p\n", other_reg);

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
        if (!as_region_link (other_reg, reg)) {
            seL4_SetMR (0, 0);
            return failure;
        }

        printf ("giving back 0x%x (size 0x%x)\n", reg_start, reg->size);
        seL4_SetMR (0, reg_start);
        seL4_SetMR (1, reg->size);
        return reply;
    }

    seL4_SetMR (0, 0);
    return failure;
}