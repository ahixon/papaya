#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sel4/sel4.h>
#include "vm.h"

#define FRAME_SIZE      (1 << seL4_PageBits)

#define FRAME_ALLOCATED (1 << 0)
#define FRAME_PINNED    (1 << 1)
#define FRAME_CLEAN     (1 << 2)
#define FRAME_DIRTY     (1 << 3)

struct frameinfo {
    paddr_t paddr;
    seL4_CPtr capability;
    uint32_t flags;
};

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
frametable_dump (void);

void ft_test1(void);
void ft_test2(void);
void ft_test3(void);

#endif