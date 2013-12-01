#include <sel4/sel4.h>
#include <string.h>

#include <cspace/cspace.h>

#include "ut_manager/ut.h"

#include "mapping.h"
#include "vmem_layout.h"
#include "frametable.h"

#include <sys/panic.h>

static struct frameinfo* frametable;
static seL4_Word ft_low, ft_high;
static seL4_Word high_idx;
static int allocated = 0;

static struct frameinfo* frame_head = NULL;
static struct frameinfo* frame_tail = NULL;

#define RESERVED_FRAMES_NUM 20
seL4_Word reserved_frames[RESERVED_FRAMES_NUM] = {0};

seL4_Word frame_get_reserved (void) {
    for (int i = 0; i < RESERVED_FRAMES_NUM; i++) {
        if (reserved_frames[i] != 0) {
            seL4_Word addr = reserved_frames[i];
            reserved_frames[i] = 0;
            return addr;
        }
    }

    return 0;
}

int frame_fill_reserved (void) {
    for (int i = 0; i < RESERVED_FRAMES_NUM; i++) {
        reserved_frames[i] = ut_alloc (seL4_PageBits);
        if (!reserved_frames[i]) {
            return false;
        }
    }

    return true;
}

/* returns if address was freed into slot, false otherwise */
int frame_free_reserved (seL4_Word phys) {
    /*for (int i = 0; i < RESERVED_FRAMES_NUM; i++) {
        if (reserved_frames[i] == 0) {
            reserved_frames[i] = phys;
            return true;
        }
    }

    return false;*/
    /* XXX: need to remount into rootsvr and zero pages */
    assert (false);
    return 0;
}

/**
 * Helper function to create a physical frame, and map it into the root
 * server's address space at a given virtual address.
 */
static void
_frame_alloc_internal (vaddr_t vaddr) {
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

    /* map the page into rootsvr so we can read/write to the pagetable array */
    err = map_page (frame_cap, seL4_CapInitThreadPD,
                   vaddr, seL4_AllRights, seL4_ARM_Default_VMAttributes);

    conditional_panic (err, "could not map page");
}

void
frametable_init (seL4_Word low_arg, seL4_Word high_arg)
{
    /* create allocated frametable for range [low, high) */
    ft_low = low_arg;
    ft_high = high_arg;

    /* try to allocate a sequence of contiguous frames in the vspace */
    frametable = (struct frameinfo*)FRAMETABLE_VSTART;
    vaddr_t current_vaddr = (vaddr_t)frametable;

    high_idx = IDX_PHYS (ft_high);

    vaddr_t final_vaddr = (vaddr_t)(&frametable[high_idx]);
    while (current_vaddr <= final_vaddr) {
        _frame_alloc_internal (current_vaddr);
        current_vaddr += PAGE_SIZE;
    }
}

struct frameinfo*
frame_new (void) {
    struct frameinfo* frame = malloc (sizeof (struct frameinfo));
    if (!frame) {
        return NULL;
    }

    memset (frame, 0, sizeof (struct frameinfo));
    frame->flags = FRAME_DIRTY;     /* XXX: remove me if we implement mount-as-readonly-first */
    frame_set_refcount (frame, 1);
    return frame;
}

/**
 * Creates a new "frame" from already allocated untyped memory. Useful for DMA
 * allocator, or mapping in device registers.
 *
 * Not added to frame queue, and is thus not able to be swapped out. If this
 * isn't what you want, you can add it to the frame queue afterwards.
 *
 * This frame does not come from the frametable, and is hence not marked with 
 * the FRAME_FRAMETABLE flag.
 */
struct frameinfo*
frame_new_from_untyped (seL4_Word untyped) {
    struct frameinfo* frame = frame_new ();
    if (!frame) {
        return NULL;
    }

    frame->paddr = untyped;
    return frame;
}

void
frame_add_queue (struct frameinfo* frame) {
    if (frame_tail) {
        frame_tail->next = frame;
        frame->prev = frame_tail;
    }

    frame_tail = frame;
    frame->next = NULL;

    if (!frame_head) {
        frame_head = frame;
        frame->prev = NULL;
    }

    allocated++;
}

struct frameinfo*
frame_alloc (void)
{
    assert (frametable);

    seL4_Word untyped_addr;

    /* reserve some from untyped memory */
    untyped_addr = ut_alloc (seL4_PageBits);
    if (!untyped_addr) {
        /* oops, out of memory! */
        printf ("frame_alloc: out of memory\n");
        return 0;
    }

    frameidx_t index = IDX_PHYS (untyped_addr);
    assert (index <= high_idx);

    /* since it's in frametable, no need to alloc */
    struct frameinfo* frame = &frametable[index];
    frame->paddr = untyped_addr;
    frame->flags |= FRAME_FRAMETABLE;
    frame->flags |= FRAME_DIRTY;        /* XXX: remove me later when remap-on-write implemented */
    frame_set_refcount (frame, 1);

    frame_add_queue (frame);

    return frame;
}

/*
 * Allocates a frame in the frametable, moves the memory based frame
 * information to this new frame and frees the old one.
 */
struct frameinfo*
frame_alloc_from_existing (struct frameinfo* old) {
    struct frameinfo* new = frame_alloc ();
    if (!new) {
        return NULL;
    }

    /* membased shouldn't be in frametable - strictly speaking, OK but yeah */
    assert (!(old->flags & FRAME_FRAMETABLE));

    /* copy stuff */
    new->flags |= old->flags;
    new->file = old->file;
    new->page = old->page;

    /* TODO: do we need to handle membased having next/prev pts? */
    assert (!old->prev && !old->next);

    /* FIXME: assert old refcount was 1 */
    free (old);

    //printf ("new file = %p\n", new->file);
    return new;
}

/* OK, so:
 *  spec says to implement second chance page replacement algorithm
 *      (or, really, the clock algorithm since that's faster and basically the
 *       same thing)
 * 
 * however, second-chance is essentially just FIFO, with an adaption to ensure
 * "referenced" pages are not swapped out early.
 * 
 * instead of checking all the pages to see if they have a reference bit set
 * and skipping them unless forced to swap.. we do not keep "unreferenced"
 * pages in our frame queue, and just do FIFO (in the hope of approximating
 * LRU).
 * 
 * hence, target selection is O(1) since we just take the head of  the queue.
 * the queue will always have something in it, unless this function was called
 * inappropriately - if the queue is empty, there should be no reason to swap,
 * as all frames are unallocated.
 *
 * important note: UT allocator keeps around a separate pool for untyped
 * objects with size 1<<seL4_PageBits.
 */
struct frameinfo*
frame_select_swap_target (void) {
    struct frameinfo* target = frame_head;
    while (target && target->flags & FRAME_PINNED) {
        printf ("skipping frame %p because it was pinned\n", target);
        target = target->next;
    }

    assert (target);  /* shouldn't be called with no frames alloc'd */

    /* remove it from the current queue - will get added back at end by
     * page_map once the the free frame is added back to the pool, and a page
     * requested again */
    if (frame_head == target) {
        frame_head = target->next;
    }

    if (frame_tail == target) {
        frame_tail = target->prev;
    }

    /* cut it out */
    if (target->prev) {
        target->prev->next = target->next;
    }

    if (target->next) {
        target->next->prev = target->prev;
    }


    target->prev = NULL;
    target->next = NULL;

    return target;
}

struct mmap*
frame_create_mmap (seL4_CPtr file, seL4_Word load_offset,
    seL4_Word file_offset, seL4_Word length) {

    struct mmap* m = malloc (sizeof (struct mmap));
    if (!m) {
        return NULL;
    }

    m->file = file;
    m->load_offset = load_offset;
    m->load_length = length;
    m->file_offset = file_offset;

    return m;
}

void
frame_free (struct frameinfo* fi) {
    unsigned int refcount = frame_get_refcount (fi);
    assert (refcount > 0);

    if (refcount > 1) {
        /* just reduce refcount, don't actually free yet */
        frame_set_refcount (fi, refcount - 1);
        return;
    }

    /* free since refcount is 0 */
    if (fi->paddr) {
        if (fi->flags & FRAME_RESERVED) {
            /* try to replenish reserve frames first */
            frame_free_reserved (fi->paddr);
        } else {
            ut_free (fi->paddr, seL4_PageBits);
        }
    }

    if (fi->file) {
        printf ("!!!!!!!!!! FREEING FILE??? !!!!!!!!!!!\n");
        free (fi->file);
    }

    fi->page = NULL;

    /* remove from frame queue */
    if (!fi->next && !fi->prev) {
        /* likely a loner + externally allocated */
        printf ("was loner, skipping framequeue\n");
        goto frame_free_cleanup;
    }

    if (frame_head == fi) {
        printf ("was head\n");
        frame_head = fi->next;
    }

    if (frame_tail == fi) {
        printf ("was tail\n");
        frame_tail = fi->prev;
    }

    /* cut ourselves out */
    if (fi->prev) {
        fi->prev->next = fi->next;
    }

    fi->prev = NULL;
    fi->next = NULL;

frame_free_cleanup:
    if (fi->flags & FRAME_FRAMETABLE) {
        fi->flags &= ~FRAME_FRAMETABLE;
        allocated--;
    } else {
        free (fi);
    }
}

#if 0
/* shouldn't really be used except for debugging */
void
frametable_freeall (void) {
    for (int i = 0; i < high_idx; i++) {
        frame_free (frametable_get_frame (i));
    }
}
#endif

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