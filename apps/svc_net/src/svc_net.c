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

#include "svc_net.h"

static seL4_CPtr async_ep;
static int last_id = 0;    /* FIXME: in future, should be bitfield */
static struct saved_data* data_head;

void interrupt_handler (struct pawpaw_event* evt) {
    network_irq (evt->args[0]);
}

/* FIXME: yuck, should be hashtable */
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

    /* TODO: when unregister is implemented, make sure that you verify this
     * pointer is valid (since the client may have freed the network service
     * before we get data back in here) */
    struct saved_data *saved = (struct saved_data*)_client_badge;
    assert (saved);
    
    /* first time receiving on this connection, set up some state/buffers */
    if (!saved->share) {
        saved->share = pawpaw_share_new ();
        assert (saved->share);

        /* backing data for ringbuffer */
        saved->buffer_data = malloc (sizeof (char) * NET_BUFFER_SIZE);
        assert (saved->buffer_data);

        saved->buffer = pawpaw_cbuf_create(NET_BUFFER_SIZE, saved->buffer_data);
        assert (saved->buffer);
    }

    /* OK, copy the data in if we can, otherwise junk it */
    struct pbuf *q;
    for (q = p; q != NULL; q = q->next) {
        char* data = q->payload;
        pawpaw_cbuf_write (saved->buffer, data, q->len);
    }

    pbuf_free (p);

    /* and tell the client */
    seL4_Notify (saved->cap, saved->id);
}

int netsvc_write (struct pawpaw_event* evt) {
    assert (evt->share);

    struct saved_data* saved = get_handler (evt);
    if (!saved) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    int len = evt->args[1];

    struct pbuf *p;
    p = pbuf_alloc (PBUF_TRANSPORT, len, PBUF_REF);
    assert (p);

    /* zero-copy woo */
    p->payload = evt->share->buf;

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    if (udp_send (saved->pcb, p)){
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
        return PAWPAW_EVENT_UNHANDLED;
    }

    evt->reply = seL4_MessageInfo_new (0, 0,
        pawpaw_share_attach (saved->share), 4);

    /* can transfer at most a page at a time, truncate request if wanted more */
    seL4_Word amount = pawpaw_cbuf_count (saved->buffer);
    if (amount > PAPAYA_IPC_PAGE_SIZE) {
        amount = PAPAYA_IPC_PAGE_SIZE;
        seL4_SetMR (2, 1);
    } else {
        /* no more buffers - if they ask again we can nuke the old one */
        seL4_SetMR (2, 0);
    }

    seL4_SetMR (0, saved->share->id);
    seL4_SetMR (1, amount);
    seL4_SetMR (3, pawpaw_cbuf_count (saved->buffer));

    /* FIXME: this needs some thought - if we read one half the buffer, and
     * data comes in, and then the rest of the ring buffer read out again, then
     * the new data will go in between the two segments. Really, we should be
     * doing double buffering */

    /* read as much as possible into the buffer */
    pawpaw_cbuf_read (saved->buffer, saved->share->buf, amount);

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
            udp_remove (pcb);
            seL4_SetMR (0, -1);
            return PAWPAW_EVENT_NEEDS_REPLY;
        }

        struct ip_addr dest;
        if (evt->args[2] == 0) {
            dest = netif_default->gw;
        } else {
            /* TODO: support binding to a given IP address */
            //dest = (struct ip_addr)evt->args[2];
            assert (false);
        }

        if (udp_connect (pcb, &dest, evt->args[1])) {
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

        /* tell client was OK */
        seL4_SetMR (0, saved->id);

        return PAWPAW_EVENT_NEEDS_REPLY;
    } else {
        /* we only handle UDP for now */
        return PAWPAW_EVENT_UNHANDLED;
    }
}

/* XXX: separate region would be better, since aligned attribute at
 * compiler whim */
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
    dma_physical = pawpaw_dma_alloc (DMA_REGION, DMA_SIZE_BITS);
    assert (dma_physical);

    /* setup our userspace allocator - DOES IT LOOK LIKE I HAVE TIME
     * TO WRITE MY OWN LWIP WRAPPER, LET ALONE TRY TO UNDERSTAND LWIP?? */
    err = dma_init (dma_physical, DMA_SIZE_BITS);
    assert (!err);

    /* Initialise the network hardware - sets up interrupts */
    if (!network_init (async_ep)) {
        return -1;
    }

    /* register and listen */
    err = pawpaw_register_service (service_ep);
    assert (err);

    pawpaw_event_loop (&handler_table, interrupt_handler, service_ep);
    return 0;
}
