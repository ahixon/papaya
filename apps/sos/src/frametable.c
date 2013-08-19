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
    printf ("address of frametable = %p\n", frametable);

    vaddr_t current_vaddr = (vaddr_t)frametable;
    current_vaddr -= FRAME_SIZE;

    vaddr_t final_vaddr = (vaddr_t)(&frametable[IDX_PHYS(high)]);
    while (current_vaddr <= final_vaddr) {
        _frame_alloc_internal (current_vaddr);
        current_vaddr += FRAME_SIZE;
    }

    printf ("\nlow\t = 0x%x (IDX_PHYS = %d, vaddr = %p)\n", low, IDX_PHYS(low), &frametable[IDX_PHYS(low)]);
    printf ("high\t = 0x%x (IDX_PHYS = %d, vaddr = %p)\n\n", high, IDX_PHYS(high), &frametable[IDX_PHYS(high)]);
    //printf ("physical\tvirtual\t\tIDX_PHYS\tIDX_VIRT\tcapability\n");
}

vaddr_t
frame_alloc (void)
{
    seL4_Word untyped_addr;
    seL4_CPtr frame_cap;
    int err;

    /* reserve some from untyped memory */
    untyped_addr = ut_alloc(seL4_PageBits);
    if (!untyped_addr) {
        /* oops, out of memory! */
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
        //printf ("could not retype: %d\n", err);
        return 0;
    }

    // ok now map from vaddr into SOS
    seL4_Word index = IDX_PHYS(untyped_addr);
    vaddr_t vaddr = FRAMEWINDOW_VSTART + (index << (seL4_PageBits));
    err = map_page(frame_cap, seL4_CapInitThreadPD,
                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);

    if (err != seL4_NoError) {
        ut_free (untyped_addr, seL4_PageBits);
        //printf ("could not map page: %d\n", err);
        return 0;
    }

    // and record the new frame
    struct frameinfo* frame = &frametable[index];

    frame->flags |= FRAME_MAPPED;
    frame->capability = frame_cap;
    frame->paddr = untyped_addr;

    //printf ("0x%x\t0x%x\t%d\t\t%d\t\t0x%x\n", untyped_addr, vaddr, IDX_PHYS(untyped_addr), IDX_VIRT(vaddr), frame_cap);
    return vaddr;
}

/* FIXME: if we have the input be the physical address, we don't even need to
   store it in the struct at all?? */

void
frame_free (vaddr_t vaddr) {
    seL4_Word idx = IDX_VIRT(vaddr);
    if (idx > high) {
        return;
    }

    struct frameinfo* fi = &frametable[idx];

    if (!(fi->flags & FRAME_MAPPED)) {
        /* don't unmap an unmapped frame */
        return;
    }

    /* first, unmap the frame on our window */
    if (seL4_ARM_Page_Unmap(fi->capability)) {
        /* FIXME: what to do if we couldn't unmap??? */
        return;
    }

    if (cspace_delete_cap (cur_cspace, fi->capability)) {
        return;
    }

    fi->flags &= ~FRAME_MAPPED;
    ut_free (fi->paddr, seL4_PageBits);
}

/* shouldn't really be used except for debugging */
void
frametable_freeall (void) {
    int numentries = IDX_PHYS(high);
    for (int i = 0; i < numentries; i++) {
        //printf ("freeing frame idx #0x%x\n", i);
        vaddr_t vaddr = i << seL4_PageBits;
        vaddr += FRAMEWINDOW_VSTART;

        frame_free (vaddr);
    }

    //printf ("all done!\n");
}