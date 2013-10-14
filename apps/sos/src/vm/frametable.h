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
    seL4_CPtr capability;
    uint32_t flags;
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
frametable_init (void);

void
frametable_freeall (void);

frameidx_t
frame_alloc (void);

void
frame_free (frameidx_t idx);

seL4_CPtr
frametable_fetch_cap (frameidx_t frame);

void
frametable_stats (void);

void ft_test1(void);
void ft_test2(void);
void ft_test3(void);

#endif