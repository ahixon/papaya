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

    /* round len up to page size */
    size_t len = evt->args[1];
    len = (len + PAGE_SIZE - 1) & PAGE_MASK;

    if (len == 0) {
        printf ("%s: invalid size 0x%x\n", __FUNCTION__, len);
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* FIXME: need memmapped region type */
    struct as_region* reg = as_define_region_within_range (current_thread->as,
        DEVICE_START, DEVICE_END, len, seL4_AllRights, REGION_GENERIC);

    if (!reg) {
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* don't cache */
    reg->attributes = 0;

    paddr_t paddr = evt->args[0];
    vaddr_t end = reg->vbase + len;

    //printf ("%s: mapping 0x%x -> 0x%x (using size 0x%x though asked for 0x%x) on paddr 0x%x\n", __FUNCTION__, reg->vbase, reg->vbase + reg->size, len, evt->args[1], paddr);
    
    for (vaddr_t vaddr = reg->vbase; vaddr < end; vaddr += PAGE_SIZE) {
        struct pt_entry* pte = page_fetch_entry (current_thread->as, reg->attributes, current_thread->as->pagetable, vaddr);
        assert (pte);

        if (pte->cap || pte->frame) {
            /* free underlying frame - should flush cache, right? */
            //printf ("%s: vaddr 0x%x already allocated, freeing\n", __FUNCTION__, vaddr);
            page_unmap (pte);
            assert (!pte->cap);
        }

        pte->frame = frame_new_from_untyped (paddr);
        if (!pte->frame) {
            printf ("%s: failed to allocate new untyped frame for vaddr 0x%x\n", __FUNCTION__, vaddr);
        }

        struct pt_entry* map_frame = page_map (current_thread->as, reg, vaddr);
        assert (map_frame);

        //printf ("%s: underlying frame for 0x%x is now 0x%x\n", __FUNCTION__, vaddr, paddr);
        paddr += PAGE_SIZE;
    }

    seL4_SetMR (0, reg->vbase);

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

    //printf("%s: asked to DMA alloc vaddr 0x%x -> 0x%x\n", __FUNCTION__, evt->args[0], end);

    /* XXX: need to write an allocator - as a hack, just give it what it asked as everything */
    seL4_Word local_dma = dma_addr;
    seL4_Word inital_dma = dma_addr;
    dma_addr = 0;

    /* go through all the underlying pages + preallocate frames */
    for (vaddr_t vaddr = evt->args[0]; vaddr < end; vaddr += PAGE_SIZE) {
        /* FIXME: should be page_fetch_entry? well probably not actually since user is mapping in already given region - BUT WHEN WE DEMAND LOAD STUFF THEN NO! */
        struct pt_entry* pte = page_fetch (current_thread->as->pagetable, vaddr);
        assert (pte);

        if (pte->cap || pte->frame) {
            /* free underlying frame - should flush cache, right? */
            //printf ("%s: vaddr 0x%x already allocated, freeing\n", __FUNCTION__, vaddr);
            page_unmap (pte);
            assert (!pte->frame);
        }

        pte->frame = frame_new_from_untyped (local_dma);
        if (!pte->frame) {
            printf ("%s: failed to allocate new untyped frame for vaddr 0x%x\n", __FUNCTION__, vaddr);
        }

        struct pt_entry* map_frame = page_map (current_thread->as, reg, vaddr);
        assert (map_frame);

        /* GOD DAMN CACHES - FIXME: only do if we remapped */
        seL4_ARM_Page_FlushCaches(map_frame->cap);

        //printf ("%s: underlying frame for 0x%x is now 0x%x\n", __FUNCTION__, vaddr, local_dma);
        local_dma += PAGE_SIZE;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, inital_dma);

    return PAWPAW_EVENT_NEEDS_REPLY;
}