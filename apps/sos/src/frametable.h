#ifndef _FRAMETABLE_H_
#define _FRAMETABLE_H_

#include <sel4/sel4.h>

#define FRAME_SIZE      (1 << seL4_PageBits)

#define FRAME_MAPPED    (1 << 0)
#define FRAME_PINNED    (1 << 1)
#define FRAME_CLEAN     (1 << 2)
#define FRAME_DIRTY     (1 << 3)

typedef seL4_Word paddr_t;
typedef seL4_Word vaddr_t;
typedef seL4_Word frameidx_t;

struct frameinfo {
    paddr_t paddr;
    seL4_Word capability;
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

seL4_Word
frametable_fetch_cap (frameidx_t frame);

void ft_test1(void);
void ft_test2(void);
void ft_test3(void);

#endif