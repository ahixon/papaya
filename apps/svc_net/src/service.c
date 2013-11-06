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
int last_id = 0;    /* FIXME: in future, should be bitfield */

int netsvc_register (struct pawpaw_event* evt);
int netsvc_read (struct pawpaw_event* evt);
int netsvc_write (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[4] = {
    {   netsvc_register,    3, HANDLER_REPLY   },      // net register svc
    {   0,  0,  0   },      // net unregister svc
    {   netsvc_read,        1,  HANDLER_REPLY   },      /* optionally needs to accept a buffer */
    {   netsvc_write,       3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },      /* optionally needs to accept a buffer */
    // net state query
    // debug stuff here (ie benchmark)
};

struct pawpaw_event_table handler_table = { 4, handlers, "net" };

void interrupt_handler (struct pawpaw_event* evt) {
    network_irq (evt->args[0]);
}

/* FIXME: need a hash table */
struct saved_data {
    struct pawpaw_share* share;
    struct pawpaw_cbuf* buffer;
    char* buffer_data;
    seL4_CPtr badge;
    seL4_CPtr cap;
    void* pcb;
    unsigned int id;    /* ID of registered event thing */

    struct saved_data* next;
};

struct saved_data* data_head;

struct saved_data*
get_handler (struct pawpaw_event* evt) {
    struct saved_data* saved = data_head;
    while (saved) {
        if (saved->badge == evt->badge && saved->id == evt->args[0]) {
            break;
        }

        saved = saved->next;
    }

    return saved;
}

static void 
recv_handler (void* _client_badge, struct udp_pcb* pcb, 
                    struct pbuf *p, struct ip_addr* ipaddr, u16_t unused2) {

    printf ("net: received data of size 0x%x\n", p->len);
    struct saved_data *saved = (struct saved_data*)_client_badge;
    assert (saved); /* FIXME: need to make sure that if we remove client that
                       we don't try to access invalid memory since we'll have
                       freed it */
    
    if (!saved->share) {
        /* FIXME: not atomic */
        saved->share = pawpaw_share_new ();
        printf ("net: making new buffer for badge %d, had ID 0x%x @ %p\n", saved->badge, saved->share->id, saved->share->buf);
        assert (saved->share);

        /* backing data for ringbuffer */
        saved->buffer_data = malloc (sizeof (char) * 0x1000);
        assert (saved->buffer_data);

        saved->buffer = pawpaw_cbuf_create (0x1000, saved->buffer_data);
        assert (saved->buffer);
    }

    /* OK, copy the data in if we can, otherwise junk it */
    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
        char* data = q->payload;
        pawpaw_cbuf_write (saved->buffer, data, q->len);
        //printf ("net: just wrote '%s'\n", data);
    }

    pbuf_free (p);

    /* and tell the client */
    printf ("got data for ID 0x%x\n", saved->id);
    seL4_Notify (saved->cap, saved->id);
}

int netsvc_write (struct pawpaw_event* evt) {
    assert (evt->share);

    struct saved_data* saved = get_handler (evt);
    if (!saved) {
        printf ("net: nobody matched badge\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    int len = evt->args[1];
    printf ("net: sending len 0x%x\n", len);

    struct pbuf *p;
    p = pbuf_alloc (PBUF_TRANSPORT, len, PBUF_REF);
    assert (p);

    /* zero-copy woo */
    p->payload = evt->share->buf;

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    if (udp_send (saved->pcb, p)){
        printf ("net: failed to send UDP packet\n");
        seL4_SetMR (0, -1);
    } else {
        seL4_SetMR (0, 0);
    }

    pbuf_free (p);

    //evt->flags |= PAWPAW_EVENT_UNMOUNT;         /* user buf */
    return PAWPAW_EVENT_NEEDS_REPLY;
}

/*
 * Fetches given data after we've saved it in our interrupt handler.
 * Reads out from the per-client ringbuffer into their sharebuf */
int netsvc_read (struct pawpaw_event* evt) {
    struct saved_data* saved = get_handler (evt);
    if (!saved) {
        printf ("net: nobody matched badge\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* FIXME: needs to send start instead */
    evt->reply = seL4_MessageInfo_new (0, 0, 1, 3);
    seL4_SetCap (0, saved->share->cap);

    printf ("net: reading out of buf belonging to conn 0x%x\n", saved->id);
    seL4_SetMR (0, saved->share->id);
    seL4_SetMR (1, pawpaw_cbuf_count (saved->buffer));
    seL4_SetMR (2, 0);  /* no more buffers - if they ask again we can nuke the old one */

    /* copy it all in */
    printf ("net: reading into buf ID 0%x @ %p\n", saved->share->id, saved->share->buf);
    pawpaw_cbuf_read (saved->buffer, saved->share->buf, pawpaw_cbuf_count (saved->buffer));
    //printf ("net: is now %s\n", saved->share->buf);

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int netsvc_register (struct pawpaw_event* evt) {
    assert (seL4_MessageInfo_get_extraCaps (evt->msg)  == 1);
    seL4_CPtr client_cap = pawpaw_event_get_recv_cap ();
    seL4_Word owner = evt->badge;
    
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    seL4_Word type = evt->args[0];
    if (type == NETSVC_PROTOCOL_UDP) {
        struct udp_pcb* pcb = udp_new();
        assert (pcb);

        if (udp_bind (pcb, &netif_default->ip_addr, evt->args[1])) {
            printf ("svc_net: udp_bind failed\n");
            udp_remove (pcb);
            seL4_SetMR (0, -1);
            return PAWPAW_EVENT_NEEDS_REPLY;
        }

        struct ip_addr dest;
        if (evt->args[2] == 0) {
            dest = netif_default->gw;
        } else {
            //dest = (struct ip_addr)evt->args[2];
            assert (false);
        }

        if (udp_connect (pcb, &dest, evt->args[1])) {
            printf ("svc_net: udp_connect failed\n");
            udp_remove (pcb);
            seL4_SetMR (0, -1);
            return PAWPAW_EVENT_NEEDS_REPLY;
        }


        /* register the thing */
        struct saved_data* saved = malloc (sizeof (struct saved_data));
        assert (saved);

        saved->share = NULL;
        saved->buffer = NULL;
        saved->badge = owner;
        saved->cap = client_cap;
        saved->pcb = pcb;
        saved->id = last_id;
        last_id++;

        saved->next = data_head;
        data_head = saved;
        
        /* FIXME: check error */
        udp_recv (pcb, &recv_handler, saved);
        printf ("svc_net: registered a UDP handler on port %u, id = 0x%x\n", evt->args[1], saved->id);

        /* tell client was OK */
        seL4_SetMR (0, saved->id);

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
