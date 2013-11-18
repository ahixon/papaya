#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sel4/sel4.h>
#include "vm.h"

#define FRAME_SIZE      (1 << seL4_PageBits)

#define FRAME_ALLOCATED (1 << 31)
#define FRAME_PINNED    (1 << 30)
#define FRAME_CLEAN     (1 << 29)
#define FRAME_DIRTY     (1 << 28)
#define NUM_FRAME_FLAGS	(4)

#define FRAME_REFCOUNT_MAX	(1 << ((32 - NUM_FRAME_FLAGS)))
#define FRAME_REFCOUNT_MASK	((1 << ((32 - NUM_FRAME_FLAGS) + 1)) - 1)

struct frameinfo {
    paddr_t paddr;
    uint32_t flags;

    /* for mmaped files - this gets converted to a physical frame when
     * we get a share (we essentially steal its frame + consume it) */
    seL4_CPtr file;
    int file_offset;
    int load_offset;   /* FIXME: could fit in flags? */
    int load_length;

    /* for paging queue */
    struct frameinfo* next;
    struct frameinfo* prev; /* needed to free things efficiently */
};

static inline int frame_get_refcount (struct frameinfo* fi) {
	return fi->flags & FRAME_REFCOUNT_MASK;
}

#include <assert.h>
static inline void frame_set_refcount (struct frameinfo* fi, int count) {
	assert (count <= FRAME_REFCOUNT_MAX);	/* FIXME: shouldn't be an assert really.. */
	fi->flags = (fi->flags & ~FRAME_REFCOUNT_MASK) | count;
}

struct frameinfo*
frametable_get_frame (frameidx_t frame);

void
frametable_init (seL4_Word low_arg, seL4_Word high_arg);

struct frameinfo*
frame_new_from_untyped (seL4_Word untyped);

void
frametable_freeall (void);

struct frameinfo*
frame_alloc (void);

struct frameinfo*
frame_alloc_from_untyped (struct frameinfo* frame, seL4_Word untyped);

void
frame_free (struct frameinfo* frame);

seL4_CPtr
frametable_fetch_cap (struct frameinfo* frame);

void
frametable_stats (void);

void ft_test1(void);
void ft_test2(void);
void ft_test3(void);

#endif