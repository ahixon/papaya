#ifndef __OS_IFACE_H__
#define __OS_IFACE_H__

#include <ethdrivers/raw_iface.h>

#define RESOURCE(x)     os_ioremap((eth_paddr_t)x##_PADDR, x##_SIZE)

extern struct ethif_os_interface _os_iface;

static inline void os_iface_init(struct ethif_os_interface os_iface){
    _os_iface = os_iface;
}

static inline void* os_malloc(size_t size){
    return _os_iface.malloc(_os_iface.cookie, size);
}

static inline void* os_ioremap(eth_paddr_t addr, int size){
    return _os_iface.ioremap(_os_iface.cookie, addr, size);
}

static inline dma_addr_t os_dma_malloc(size_t size, int cached){
    return _os_iface.dma_malloc(_os_iface.cookie, size, cached);
}


static inline eth_paddr_t os_phys(const dma_addr_t* dma_addr){
    return dma_addr->phys;
}

static inline eth_paddr_t os_virt(const dma_addr_t* dma_addr){
    return dma_addr->virt;
}

static inline int os_dma_valid(const dma_addr_t* dma){
    return os_phys(dma) && os_virt(dma);
}

static inline void os_clean(eth_vaddr_t addr, int range){
    _os_iface.clean(_os_iface.cookie, addr, range);
}

static inline void os_invalidate(eth_vaddr_t addr, int range){
    _os_iface.invalidate(_os_iface.cookie, addr, range);
}


#endif /* __OS_IFACE_H__ */
