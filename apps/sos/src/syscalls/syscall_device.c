#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <mapping.h>
#include <assert.h>
#include <stdio.h>

#include <vm/vmem_layout.h>
#include <vm/frametable.h>

extern thread_t current_thread;
extern seL4_Word dma_addr;

#define PAGE_MASK (~(PAGE_SIZE - 1))

int syscall_register_irq (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    /* FIXME: probably want to copy instead, so that we can revoke later if
     * needed */
    seL4_CPtr irq_cap = cspace_irq_control_get_cap (cur_cspace,
        seL4_CapIRQControl, evt->args[0]);

    if (!irq_cap) {
        seL4_SetMR (0, 0);
    } else {
        seL4_CPtr their_cap = cspace_copy_cap (current_thread->croot,
            cur_cspace, irq_cap, seL4_AllRights);

        seL4_SetMR (0, their_cap);
    }

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_map_device (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    /* round len up to page size */
    size_t len = evt->args[1];
    len = (len + PAGE_SIZE - 1) & PAGE_MASK;

    if (len == 0) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* TODO: need memmapped region type */
    struct as_region* reg = as_define_region_within_range (current_thread->as,
        DEVICE_START, DEVICE_END, len, seL4_AllRights, REGION_GENERIC);

    if (!reg) {
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* don't cache, unless you want to spend days debugging... eugh... */
    reg->attributes = 0;

    paddr_t paddr = evt->args[0];
    vaddr_t end = reg->vbase + len;
    
    for (vaddr_t vaddr = reg->vbase; vaddr < end; vaddr += PAGE_SIZE) {
        struct pt_entry* pte = page_fetch_new (current_thread->as,
            reg->attributes, current_thread->as->pagetable, vaddr);

        assert (pte);

        if (pte->cap || pte->frame) {
            /* free underlying frame - should flush cache, right? */
            page_free (pte);
            assert (!pte->cap);
        }

        pte->frame = frame_new_from_untyped (paddr);

        int status = PAGE_FAILED;
        struct pt_entry* page = page_map (current_thread->as, reg, vaddr,
            &status, NULL, NULL);

        /* FIXME: should be able to handle swap outs!!! */

        assert (status == PAGE_SUCCESS);
        assert (page);

        paddr += PAGE_SIZE;
    }

    seL4_SetMR (0, reg->vbase);

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_alloc_dma (struct pawpaw_event* evt) {
    if (!dma_addr) {
        /* DMA already all allocated */
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct as_region* reg = as_get_region_by_addr (current_thread->as,
        evt->args[0]);

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

    /* XXX: need to write an allocator - as a hack, just give it what it asked
     * as everything */
    seL4_Word local_dma = dma_addr;
    seL4_Word inital_dma = dma_addr;
    dma_addr = 0;

    /* go through all the underlying pages + preallocate frames */
    for (vaddr_t vaddr = evt->args[0]; vaddr < end; vaddr += PAGE_SIZE) {
        struct pt_entry* pte = page_fetch_new (current_thread->as,
            reg->attributes, current_thread->as->pagetable, vaddr);

        assert (pte);

        if (pte->cap || (pte->frame && pte->frame->paddr)) {
            page_free (pte);
            if (pte->frame) {
                assert (!pte->frame->paddr);
            }
        }

        pte->frame = frame_new_from_untyped (local_dma);

        int status = PAGE_FAILED;
        struct pt_entry* page = page_map (current_thread->as, reg, vaddr,
            &status, NULL, NULL);

        /* FIXME: should be able to handle swap outs!!! */
        
        assert (status == PAGE_SUCCESS);
        assert (page);

        /* flush the cache! FIXME: really only do this if we remapped */
        seL4_ARM_Page_FlushCaches (page->cap);

        local_dma += PAGE_SIZE;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, inital_dma);

    return PAWPAW_EVENT_NEEDS_REPLY;
}