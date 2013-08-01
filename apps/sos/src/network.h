#ifndef NETWORK_H
#define NETWORK_H

#include <sel4/types.h>
#include <nfs/nfs.h>

extern fhandle_t mnt_point;


extern void network_init(seL4_CPtr interrupt_ep);

extern int dma_init(seL4_Word paddr, int sizebits);

// Always call network_irq if an interrupt occurs that you are not interested in
extern void network_irq(seL4_Word irq);

#endif
