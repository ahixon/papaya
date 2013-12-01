#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sel4/sel4.h>
#include "vm.h"

#define FRAME_FRAMETABLE    (1 << 31)   /* in frametable / "referenced" bit */
#define FRAME_SWAPPING      (1 << 30)
#define FRAME_PINNED        (1 << 29)
#define FRAME_DIRTY         (1 << 28)   /* unused */
#define FRAME_COPY_ON_WRITE (1 << 27)   /* unused, relevant iff refcount > 1 */
#define FRAME_PAGELIST      (1 << 26)
#define FRAME_RESERVED      (1 << 25)
#define NUM_FRAME_FLAGS     (8)

#define FRAME_REFCOUNT_MAX	(1 << ((32 - NUM_FRAME_FLAGS)))
#define FRAME_REFCOUNT_MASK	((1 << ((32 - NUM_FRAME_FLAGS) + 1)) - 1)

#define IDX_PHYS(x) ((x - ft_low) >> seL4_PageBits)
#define IDX_VIRT(x) ((x - FRAMEWINDOW_VSTART) >> seL4_PageBits)

struct mmap {
    seL4_CPtr file;     /* badged endpoint to open file */
    int file_offset;
    int load_offset;    /* offset of where to load into buffer */
    int load_length;    /* how many bytes to read into buffer */
};

struct frameinfo {
    uint32_t flags;

    paddr_t paddr;
    struct mmap* file;      /* not a union since although a page might be
                               mapped in; if it's clean we can just junk it and
                               not load from swap (and hence can't throw away
                               mmaped file info) */

    union {
        struct pt_entry* page;
        struct pagelist* pages; /* pages associated with this frame */
    };

    /* for paging queue */
    struct frameinfo* next;
    struct frameinfo* prev; /* needed to free things efficiently */
};

static inline int frame_get_refcount (struct frameinfo* fi) {
	return fi->flags & FRAME_REFCOUNT_MASK;
}

#include <assert.h>
static inline void frame_set_refcount (struct frameinfo* fi, int count) {
    /* FIXME: shouldn't be an assert really, nor belong here.. */
	assert (count <= FRAME_REFCOUNT_MAX);

	fi->flags = (fi->flags & ~FRAME_REFCOUNT_MASK) | count;
}

int frame_fill_reserved (void);

int frame_free_reserved (seL4_Word phys);


struct frameinfo*
frametable_get_frame (frameidx_t frame);

seL4_Word frame_get_reserved (void);

void
frametable_init (seL4_Word low_arg, seL4_Word high_arg);

struct frameinfo*
frame_new (void);

void
frame_add_queue (struct frameinfo* frame);


struct frameinfo*
frame_new_from_untyped (seL4_Word untyped);

void
frametable_freeall (void);

struct frameinfo*
frame_alloc (void);

struct frameinfo*
frame_alloc_from_existing (struct frameinfo* old);

struct frameinfo*
frame_select_swap_target (void);

struct mmap*
frame_create_mmap (seL4_CPtr file, seL4_Word load_offset,
    seL4_Word file_offset, seL4_Word length);

void
frame_free (struct frameinfo* frame);

void
frametable_stats (void);

#endif