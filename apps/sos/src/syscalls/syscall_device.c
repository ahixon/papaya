#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <mapping.h>
#include <assert.h>
#include <stdio.h>

extern thread_t current_thread;
extern seL4_Word dma_addr;

int syscall_register_irq (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    /* FIXME: probably want to copy instead, so that we can revoke later if needed */
    seL4_CPtr irq_cap = cspace_irq_control_get_cap (cur_cspace, seL4_CapIRQControl, evt->args[0]);
    if (!irq_cap) {
        seL4_SetMR (0, 0);
    } else {
        seL4_CPtr their_cap = cspace_copy_cap (current_thread->croot, cur_cspace, irq_cap, seL4_AllRights);
        seL4_SetMR (0, their_cap);
    }

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_map_device (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, (seL4_Word)map_device_thread ((void*)evt->args[0], evt->args[1], current_thread));

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_alloc_dma (struct pawpaw_event* evt) {
    if (!dma_addr) {
        printf("%s: DMA already all allocated\n", __FUNCTION__);
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct as_region* reg = as_get_region_by_addr (current_thread->as, evt->args[0]);
    if (!reg) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* make sure we don't cache DMA regions */
    reg->attributes = 0;

    /* whole extent not in region */
    vaddr_t end = evt->args[0] + (1 << evt->args[1]);
    if (end >= (reg->vbase + reg->size)) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    printf("%s: asked to DMA alloc vaddr 0x%x -> 0x%x\n", __FUNCTION__, evt->args[0], end);

    /* XXX: need to write an allocator - as a hack, just give it what it asked as everything */
    seL4_Word local_dma = dma_addr;
    seL4_Word inital_dma = dma_addr;
    dma_addr = 0;

    /* go through all the underlying pages + preallocate frames */
    for (vaddr_t vaddr = evt->args[0]; vaddr < end; vaddr += PAGE_SIZE) {
        struct pt_entry* pte = page_fetch (current_thread->as->pagetable, vaddr);
        assert (pte);

        if (pte->frame) {
            /* free underlying frame - should flush cache, right? */
            //printf ("%s: vaddr 0x%x already allocated, freeing\n", vaddr);
            page_unmap (pte);
            assert (!pte->frame);
        }

        pte->frame = frame_new_from_untyped (local_dma);
        if (!pte->frame) {
            printf ("%s: failed to allocate new untyped frame for vaddr 0x%x\n", __FUNCTION__, vaddr);
        }

        /*struct frame_info* map_frame = page_map (current_thread->as, reg, vaddr);
        assert (map_frame);*/

        //printf ("%s: underlying frame for 0x%x is now 0x%x\n", __FUNCTION__, vaddr, local_dma);
        local_dma += PAGE_SIZE;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, inital_dma);

    return PAWPAW_EVENT_NEEDS_REPLY;
}