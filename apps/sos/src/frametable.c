#include <sel4/sel4.h>

#include <cspace/cspace.h>

#include "ut_manager/ut.h"

#include "mapping.h"
#include "vmem_layout.h"
#include "frametable.h"
#include "freeframe.h"

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

#define FRAME_SIZE          (1 << seL4_PageBits)

#define IDX_PHYS(x) ((x - low) >> seL4_PageBits)
#define IDX_VIRT(x) ((x - FRAMEWINDOW_VSTART) >> seL4_PageBits)

struct frameinfo* frametable;
seL4_Word low, high;        /* FIXME: really should be public globals rather than re-declaring what ut_manages has (_low, _high) */
frameidx_t high_idx;

static void
_frame_alloc_internal (vaddr_t prev) {
    int err;
    seL4_CPtr frame_cap;

    /* reserve some from untyped memory */
    seL4_Word untyped_addr = ut_alloc(seL4_PageBits);
    conditional_panic (!untyped_addr, "no memory to alloc frametable frame");

    /* awesome; we have memory, now retype it so we get a vaddr */
    err =  cspace_ut_retype_addr(untyped_addr,
                                 seL4_ARM_SmallPageObject,
                                 seL4_PageBits,
                                 cur_cspace,
                                 &frame_cap);
    conditional_panic (err, "could not retype frametable frame");

    /* map into SOS AS A CONTIGUOUS SECTION */
    vaddr_t vaddr = prev + FRAME_SIZE;
    //printf ("mapped page to vaddr 0x%x\n", vaddr);
    err = map_page(frame_cap, seL4_CapInitThreadPD,
                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    conditional_panic (err, "could not map page");
}

void
frametable_init (void)
{
    /* create free frame stack */
    //freeframe_init ();

    /* create allocated frametable for range [low, high) */
    ut_find_memory (&low, &high);

    /* try to allocate a sequence of contiguous frames in the vspace */
    frametable = (struct frameinfo*)FRAMETABLE_VSTART;

    vaddr_t current_vaddr = (vaddr_t)frametable;
    current_vaddr -= FRAME_SIZE;

    high_idx = IDX_PHYS(high);

    vaddr_t final_vaddr = (vaddr_t)(&frametable[high_idx]);
    while (current_vaddr <= final_vaddr) {
        _frame_alloc_internal (current_vaddr);
        current_vaddr += FRAME_SIZE;
    }

    printf ("low\t = 0x%x (IDX_PHYS = %d, vaddr = %p)\n", low, IDX_PHYS(low), &frametable[IDX_PHYS(low)]);
    printf ("high\t = 0x%x (IDX_PHYS = %d, vaddr = %p)\n\n", high, IDX_PHYS(high), &frametable[IDX_PHYS(high)]);
    //printf ("physical\tvirtual\t\tIDX_PHYS\tIDX_VIRT\tcapability\n");
}

frameidx_t
frame_alloc (void)
{
    seL4_Word untyped_addr;
    seL4_CPtr frame_cap;
    int err;

    /* reserve some from untyped memory */
    untyped_addr = ut_alloc(seL4_PageBits);
    if (!untyped_addr) {
        /* oops, out of memory! */
        printf ("frame_alloc: out of memory\n");
        return 0;
    }

    /* awesome; we have memory, now retype it so we get a vaddr */
    err =  cspace_ut_retype_addr(untyped_addr,
                                 seL4_ARM_SmallPageObject,
                                 seL4_PageBits,
                                 cur_cspace,
                                 &frame_cap);
    if (err != seL4_NoError) {
        ut_free (untyped_addr, seL4_PageBits);
        printf ("frame_alloc: could not retype: %s\n", seL4_Error_Message(err));
        return 0;
    }

    frameidx_t index = IDX_PHYS(untyped_addr);
    struct frameinfo* frame = &frametable[index];

    frame->flags |= FRAME_MAPPED;
    frame->capability = frame_cap;
    frame->paddr = untyped_addr;

    printf ("frame_alloc: allocated physical frame at 0x%x and cap = 0x%x, returning index 0x%x\n", untyped_addr, frame_cap, index);
    return index;
}

void
frame_free (frameidx_t idx) {
    if (idx > high_idx) {
        return;
    }

    struct frameinfo* fi = &frametable[idx];

    if (!(fi->flags & FRAME_MAPPED)) {
        printf ("frame_free: frame already mapped!\n");
        /* don't unmap an unmapped frame */
        return;
    }

    /* first, unmap the frame on our window */
    if (seL4_ARM_Page_Unmap(fi->capability)) {
        /* FIXME: what to do if we couldn't unmap??? */
        printf ("frame_free: unmap failed\n");
        return;
    }

    if (cspace_delete_cap (cur_cspace, fi->capability)) {
        printf ("frame_free: could not delete cap\n");
        return;
    }

    fi->flags &= ~FRAME_MAPPED;
    ut_free (fi->paddr, seL4_PageBits);
}

/* shouldn't really be used except for debugging */
void
frametable_freeall (void) {
    for (int i = 0; i < high_idx; i++) {
        frame_free (i);
    }
}

seL4_CPtr
frametable_fetch_cap (frameidx_t frame) {
    if (frame > high_idx) {
        return 0;
    }

    return frametable[frame].capability;
}