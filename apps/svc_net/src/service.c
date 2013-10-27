#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>
#include <pawpaw.h>
#include <sos.h>

#include <lwip/netif.h>
#include <lwip/pbuf.h>
#include <lwip/udp.h>

#include <dma.h>
#include "network.h"
#include <network.h>

seL4_CPtr async_ep;

int netsvc_register (struct pawpaw_event* evt);
int netsvc_read (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[3] = {
    {   netsvc_register,    2, HANDLER_REPLY   },      // net register svc
    {   0,  0,  0   },      // net unregister svc
    {   netsvc_read,        0,  HANDLER_REPLY   },      /* optionally needs to accept a buffer */
    // net state query
    // debug stuff here (ie benchmark)
};

struct pawpaw_event_table handler_table = { 3, handlers };

void interrupt_handler (struct pawpaw_event* evt) {
    network_irq (evt->args[0]);
}

/* FIXME: need a hash table */
struct saved_data {
    struct pawpaw_share* share;
    struct pawpaw_cbuf* buffer;
    seL4_CPtr badge;
    seL4_CPtr cap;
    int unread;         /* FIXME: cbuf should be IN share to have same state */

    struct saved_data* next;
};

struct saved_data* data_head;

static void 
recv_handler (void* _client_badge, struct udp_pcb* pcb, 
                    struct pbuf *p, struct ip_addr* ipaddr, u16_t unused2) {

    printf ("svc_net: got some UDP data\n");
    seL4_Word badge = (seL4_Word)_client_badge;
    /* keep the data around for when they ask for it */
    struct saved_data* saved = data_head;
    while (saved) {
        if (saved->badge == badge) {
            break;
        }

        saved = saved->next;
    }

    assert (saved);

    if (!saved->share) {
        /* FIXME: not atomic */
        saved->share = pawpaw_share_new ();
        assert (saved->share);

        /* FIXME: yuck */
        saved->buffer = pawpaw_cbuf_create (0x1000, saved->share->buf);
        assert (saved->buffer);
    }

    /* OK, copy the data in if we can, otherwise junk it */
    struct pbuf *q;
    int total_len = 0;
    for (q = p; q != NULL; q = q->next) {
        char* data = q->payload;
        pawpaw_cbuf_write (saved->buffer, data, q->len);
        //printf ("just wrote '%s'\n", data, saved->share->buf);
        total_len += q->len;
    }

    pbuf_free (p);

    /* and tell the client */
    printf ("notifying...\n");
    saved->unread += total_len;
    seL4_Notify (saved->cap, 0);
}

int netsvc_read (struct pawpaw_event* evt) {
    struct saved_data* saved = data_head;
    while (saved) {
        if (saved->badge == evt->badge) {
            break;
        }

        saved = saved->next;
    }

    if (!saved) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* FIXME: needs to send start instead */
    evt->reply = seL4_MessageInfo_new (0, 0, pawpaw_share_attach (saved->share), 2);
    seL4_SetMR (0, saved->share->id);
    //seL4_SetMR (1, pawpaw_cbuf_count (saved->buffer));
    seL4_SetMR (1, saved->unread);
    seL4_SetMR (2, 0);  /* no more buffers - if they ask again we can nuke the old one */

    saved->unread = 0;

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int netsvc_register (struct pawpaw_event* evt) {
    seL4_CPtr client_cap = pawpaw_event_get_recv_cap ();
    seL4_Word owner = evt->badge;

    seL4_Word type = evt->args[0];
    if (type == NETSVC_PROTOCOL_UDP) {
        struct udp_pcb* pcb = udp_new();
        assert (pcb);

        if (udp_bind (pcb, &netif_default->ip_addr, evt->args[1])) {
            printf ("svc_net: udp_bind failed\n");
            udp_remove (pcb);
            return PAWPAW_EVENT_UNHANDLED;
        }

        if (udp_connect (pcb, &netif_default->gw, evt->args[1])) {
            printf ("svc_net: udp_connect failed\n");
            udp_remove (pcb);
            return PAWPAW_EVENT_UNHANDLED;
        }

        printf ("svc_net: registered a UDP handler on port %u\n", evt->args[1]);
        udp_recv (pcb, &recv_handler, (void*)owner);

        /* register the thing */
        struct saved_data* saved = malloc (sizeof (struct saved_data));
        assert (saved);

        saved->share = NULL;
        saved->buffer = NULL;
        saved->badge = owner;
        saved->cap = client_cap;

        saved->next = data_head;
        data_head = saved;

        /* send some crap so we know we're ready */
        char* obama = "and even though i sent you flowers...\n";
        struct pbuf *p;
        p = pbuf_alloc (PBUF_TRANSPORT, strlen(obama), PBUF_RAM);
        assert (p);

        if(pbuf_take(p, obama, strlen(obama))){
            pbuf_free(p);
            printf("failed to take pbuf\n");
            return PAWPAW_EVENT_UNHANDLED;
        }

        if (udp_send(pcb, p)){
            printf("failed to send pbuf\n");
            pbuf_free(p);
        }

        pbuf_free(p);

        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);

        return PAWPAW_EVENT_NEEDS_REPLY;
    } else {
        printf ("svc_net: got non UDP request\n");
        return PAWPAW_EVENT_UNHANDLED;
    }
}

/* XXX: separate region would be better, since aligned attribute at
 * compiler whim */
volatile char DMA_REGION[1 << DMA_SIZE_BITS] __attribute__((aligned(0x1000))) = { 0 };

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
    dma_physical = pawpaw_dma_alloc (DMA_REGION, DMA_SIZE_BITS);
    assert (dma_physical);

    /* setup our userspace allocator - DOES IT LOOK LIKE I HAVE TIME
     * TO WRITE MY OWN LWIP WRAPPER, LET ALONE TRY TO UNDERSTAND LWIP?? */
    err = dma_init (dma_physical, DMA_SIZE_BITS);
    assert (!err);

    /* Initialise the network hardware - sets up interrupts */
    // FIXME: make this return an error
    /*err = */network_init (async_ep);
    //assert (!err);

    /* register and listen */
    err = pawpaw_register_service (service_ep);
    assert (err);

    printf ("svc_net: started\n");
    pawpaw_event_loop (&handler_table, interrupt_handler, service_ep);

    return 0;
}
