/**
 * This file implements very simple DMA for sosh.
 *
 * It does not free and only keeps a memory pool
 * big enough to get the network drivers booted.
 */
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <sel4/types.h>
#include <cspace/cspace.h>
#include <dma.h>
#include <mapping.h>
#include <ut_manager/ut.h>
#include <vm/vmem_layout.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

#define DMA_SIZE     (_dma_pend - _dma_pstart)
#define DMA_PAGES    (DMA_SIZE >> seL4_PageBits)
#define DMA_VEND     (DMA_VSTART + DMA_SIZE)

#define PHYS(vaddr)  (vaddr - DMA_VSTART + _dma_pstart)
#define VIRT(paddr)  (paddr + DMA_VSTART - _dma_pstart)

#define PAGE_OFFSET(a) ((a) & ((1 << seL4_PageBits) - 1))

#define BIT(b)          (1UL << (b))
#define MASK(b)         (BIT(b) - 1)
#define ROUND_DOWN(v,b) ((v) & ~MASK(b))
#define ROUND_UP(v, b)  ROUND_DOWN((v) + MASK(b), b)

#define DMA_ALIGN_BITS  7 /* 128 */
#define DMA_ALIGN(a)    ROUND_UP(a,DMA_ALIGN_BITS)

static seL4_CPtr* _dma_caps;

static seL4_Word _dma_pstart = 0;
static seL4_Word _dma_pend = 0;
static seL4_Word _dma_pnext = 0;

void
dma_invalidate(void* cookie, vaddr_t addr, int range){
    (void)cookie;
    (void)addr;
    (void)range;
}

void
dma_clean(void* cookie, vaddr_t addr, int range){
    (void)cookie;
    (void)addr;
    (void)range;
}


static inline void 
_dma_fill(seL4_Word pstart, seL4_Word pend, int cached){
    seL4_CPtr* caps = &_dma_caps[(pstart - _dma_pstart) >> seL4_PageBits];
    seL4_ARM_VMAttributes vm_attr = 0;
    int err;

    if(cached){
        vm_attr = seL4_ARM_Default_VMAttributes;
        vm_attr = 0 /* TODO L2CC currently not controlled by kernel */;
    }

    pstart -= PAGE_OFFSET(pstart);
    while(pstart < pend){
        if(*caps == seL4_CapNull){
            /* Create the frame cap */
            err = cspace_ut_retype_addr(pstart, seL4_ARM_SmallPageObject,
                                        seL4_PageBits, cur_cspace, caps);
            assert(!err);
            /* Map in the frame */
            err = map_page(*caps, seL4_CapInitThreadPD, VIRT(pstart), 
                           seL4_AllRights, vm_attr);
            assert(!err);
        }
        /* Next */
        pstart += (1 << seL4_PageBits);
        caps++;
    }
}


int 
dma_init(seL4_Word dma_paddr_start, int sizebits){
    assert(_dma_pstart == 0);

    _dma_pstart = _dma_pnext = dma_paddr_start;
    _dma_pend = dma_paddr_start + (1 << sizebits);
    _dma_caps = (seL4_CPtr*)malloc(sizeof(seL4_CPtr) * DMA_PAGES);
    conditional_panic(!_dma_caps, "Not enough heap space for dma frame caps");

    memset(_dma_caps, 0, sizeof(seL4_CPtr) * DMA_PAGES);
    return 0;
}


struct dma_addr 
sos_dma_malloc(void* cookie, uint32_t size, int cached) {
    static int alloc_cached = 0;
    struct dma_addr dma_mem;
    (void)cookie;

    assert(_dma_pstart);
    _dma_pnext = DMA_ALIGN(_dma_pnext);
    if(_dma_pnext < _dma_pend){
        /* If caching policy has changed we round to page boundary */
        if(alloc_cached != cached && PAGE_OFFSET(_dma_pnext) != 0){
            _dma_pnext = ROUND_UP(_dma_pnext, seL4_PageBits);
        }
        alloc_cached = cached;
        /* Fill the dma memory */
        _dma_fill(_dma_pnext, _dma_pnext + size, cached);
        /* set return values */
        dma_mem.phys = (paddr_t)_dma_pnext;
        dma_mem.virt = (vaddr_t)VIRT(dma_mem.phys);
        _dma_pnext += size;
    }else{
        dma_mem.phys = 0;
        dma_mem.virt = 0;
    }
    dprintf(5, "DMA: 0x%x - 0x%x\n", (uint32_t)dma_mem.phys, 
                                     (uint32_t)dma_mem.phys + size);
    return dma_mem;
}


