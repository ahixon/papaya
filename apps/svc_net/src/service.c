#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>
#include <pawpaw.h>
#include <sos.h>

#include <dma.h>
#include "network.h"

seL4_CPtr async_ep;

struct pawpaw_eventhandler_info handlers[2] = {
    {   0,  0,  0   },      // net register svc
    {   0,  0,  0   },      // net unregister svc
    // net state query
    // debug stuff here (ie benchmark)
};

struct pawpaw_event_table handler_table = { 2, handlers };

void interrupt_handler (struct pawpaw_event* evt) {
    network_irq (evt->args[0]);
}

char DMA_REGION[1 << DMA_SIZE_BITS] __attribute__((aligned(0x1000))) = { 0 };

seL4_Word dma_physical;

int main (void) {
    int err;

    /* init event handler */
    pawpaw_event_init ();

    /* create async EP for interrupts */
    async_ep = pawpaw_create_ep_async ();
    assert (async_ep);

    /* bind async EP to any sync EPs (so we can just listen on one) */
    err = seL4_TCB_BindAEP (PAPAYA_TCB_SLOT, async_ep);
    assert (!err);

    /* create our EP to listen on */
    seL4_CPtr service_ep = pawpaw_create_ep ();
    assert (service_ep);

    /* set underlying physical memory to be contiguous for DMA */
    dma_physical = pawpaw_dma_alloc (&DMA_REGION, DMA_SIZE_BITS);
    assert (dma_physical);

    /* setup our userspace allocator - DOES IT LOOK LIKE I HAVE TIME
     * TO WRITE MY OWN LWIP WRAPPER, LET ALONE TRY TO UNDERSTAND LWIP?? */
    dma_init (dma_physical, DMA_SIZE_BITS);

    /* Initialise the network hardware - sets up interrupts */
    network_init (async_ep);

    /* register and listen */
    err = pawpaw_register_service (service_ep);
    assert (err);

    printf ("svc_net: started\n");
    pawpaw_event_loop (&handler_table, interrupt_handler, service_ep);

    return 0;
}
