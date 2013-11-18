#include <sel4/sel4.h>
#include <string.h>

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

static int allocated = 0;

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

    /* map the page into SOS so we can read/write to the pagetable array */
    vaddr_t vaddr = prev + FRAME_SIZE;
    err = map_page(frame_cap, seL4_CapInitThreadPD,
                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    conditional_panic (err, "could not map page");
}

void
frametable_init (seL4_Word low_arg, seL4_Word high_arg)
{
    /* create free frame stack */
    //freeframe_init ();

    /* create allocated frametable for range [low, high) */
    low = low_arg;
    high = high_arg;

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
}

struct frameinfo* frame_head = NULL;
struct frameinfo* frame_tail = NULL;

struct frameinfo*
frame_alloc (void)
{
    conditional_panic (!frametable, "frametable not initialised yet");

    seL4_Word untyped_addr;

    /* reserve some from untyped memory */
    untyped_addr = ut_alloc (seL4_PageBits);
    if (!untyped_addr) {
        /* oops, out of memory! */
        printf ("frame_alloc: out of memory\n");
        return 0;
    }

    frameidx_t index = IDX_PHYS(untyped_addr);

    struct frameinfo* frame = &frametable[index];
    frame = frame_alloc_from_untyped (frame, untyped_addr);

    if (!frame) {
        ut_free (untyped_addr, seL4_PageBits);
        return NULL;
    }

    /* add to frame queue */
    if (frame_tail) {
        frame_tail->next = frame;
        frame->prev = frame_tail;
    }

    frame_tail = frame;

    if (!frame_head) {
        frame_head = frame;
    }

    allocated++;
    return frame;
}

struct frameinfo*
frame_new_from_untyped (seL4_Word untyped) {
    struct frameinfo* frame = malloc (sizeof (struct frameinfo));
    if (!frame) {
        return NULL;
    }

    memset (frame, 0, sizeof (struct frameinfo));

    frame = frame_alloc_from_untyped (frame, untyped);
    if (!frame) {
        free (frame);
        return NULL;
    }

    return frame;
}

struct frameinfo*
frame_alloc_from_untyped (struct frameinfo* frame, seL4_Word untyped) {
    /*seL4_CPtr frame_cap;
    int err;

    err =  cspace_ut_retype_addr(untyped,
                                 seL4_ARM_SmallPageObject, seL4_PageBits,
                                 cur_cspace, &frame_cap);
    if (err != seL4_NoError) {
        printf ("frame_alloc: could not retype: %s\n", seL4_Error_Message (err));
        return NULL;
    }*/   

    frame->flags |= FRAME_ALLOCATED;
    //frame->capability = frame_cap;
    frame->paddr = untyped;
    frame_set_refcount (frame, 1);

    return frame;
}

void
frame_free (struct frameinfo* fi) {
    //struct frameinfo* fi = &frametable[idx];

    assert (fi->flags & FRAME_ALLOCATED);

    unsigned int refcount = frame_get_refcount (fi);
    printf ("%s: refcount for paddr 0x%x is %d\n", __FUNCTION__, fi->paddr, refcount);
    assert (refcount > 0);
    if (refcount > 1) {
        /* just reduce refcount, don't actually free yet */
        frame_set_refcount (fi, refcount - 1);
        printf ("%s: refcount now %d\n", __FUNCTION__, frame_get_refcount (fi));
        return;
    }

    /*if (cspace_revoke_cap (cur_cspace, fi->capability)) {
        printf ("frame_free: failed to revoke cap\n");
    }

    if (cspace_delete_cap (cur_cspace, fi->capability)) {
        printf ("frame_free: could not delete cap\n");
    }*/

    printf ("freeing since refcount was %u\n", refcount);
    ut_free (fi->paddr, seL4_PageBits);

    fi->flags &= ~FRAME_ALLOCATED;
    allocated--;

    /* remove from frame queue */
    if (!fi->next && !fi->prev) {
        /* likely a loner + externally allocated */
        //free (fi);
        return;
    }

    if (frame_head == fi) {
        frame_head = fi->next;
    }

    if (frame_tail == fi) {
        frame_tail = fi->prev;
    }

    /* cut ourselves out */
    if (fi->prev) {
        fi->prev->next = fi->next;
    }

    fi->prev = NULL;
    fi->next = NULL;
}

/* shouldn't really be used except for debugging */
void
frametable_freeall (void) {
    for (int i = 0; i < high_idx; i++) {
        frame_free (frametable_get_frame (i));
    }
}

void
frametable_stats (void) {
    printf ("Allocated frames: 0x%x\n", allocated);
    /*for (int i = 0; i <= high_idx; i++) {
        struct frameinfo* fi = frametable_get_frame (i);
        if (fi->flags & FRAME_ALLOCATED) {
            printf ("\tFrame 0x%x:\tpaddr 0x%x\t0x%x ref(s)\n", i, fi->paddr, frame_get_refcount (fi));
        }
    }
    printf ("\n");*/
}

struct frameinfo*
frametable_get_frame (frameidx_t frame) {
    return &frametable[frame];
}

/*
seL4_CPtr
frametable_fetch_cap (struct frameinfo* frame) {
    return frame->capability;
}*/