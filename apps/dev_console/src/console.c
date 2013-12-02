#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>

#include <pawpaw.h>
#include <syscalls.h>
#include <network.h>
#include <device.h>
#include <vfs.h>

#include <sos.h>

#include "console.h"

static struct pawpaw_cbuf* console_buffer;

static seL4_CPtr service_ep = 0;

static seL4_CPtr net_ep = 0;
static int net_id = -1;

/* simple counter to create our file descriptors.
 * TODO: in future, this should be a bitfield (max size open FDs) */
static int last_opened = 1;
static struct fhandle* open_list = NULL;

/* FIXME: yuck, this should be a hashtable */
static struct fhandle* console_lookup_fhandle (seL4_Word badge) {
    struct fhandle* h = open_list;
    while (h) {
        if (h->id == badge) {
            return h;
        }

        h = h->next;
    }

    return NULL;
}

int vfs_open (struct pawpaw_event* evt) {
    if (evt->args[0] & FM_READ) {
        if (current_reader) {
            /* only one reader permitted */
            evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
            seL4_SetMR (0, -1);
            return PAWPAW_EVENT_NEEDS_REPLY;
        }
    }

    struct fhandle* fh = malloc (sizeof (struct fhandle));
    assert (fh);
    fh->id = last_opened;
    fh->current_event = NULL;
    last_opened++;
    fh->mode = evt->args[0];

    /* add to list */
    fh->next = open_list;
    open_list = fh;

    if (evt->args[0] & FM_READ) {
        current_reader = fh;
    }

    seL4_CPtr their_cap = pawpaw_cspace_alloc_slot ();
    int err = seL4_CNode_Mint (
        PAPAYA_ROOT_CNODE_SLOT, their_cap,  PAPAYA_CSPACE_DEPTH,
        PAPAYA_ROOT_CNODE_SLOT, service_ep, PAPAYA_CSPACE_DEPTH,
        seL4_AllRights, seL4_CapData_Badge_new (fh->id));

    assert (their_cap > 0);
    assert (err == 0);

    seL4_SetCap (0, their_cap);
    seL4_SetMR (0, 0);
    evt->reply = seL4_MessageInfo_new (0, 0, 1, 1);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

void interrupt_handler (struct pawpaw_event* evt) {
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, NETSVC_SERVICE_DATA);
    seL4_SetMR (1, net_id);
    seL4_MessageInfo_t reply = seL4_Call (net_ep, msg);

    seL4_Word size = seL4_GetMR (1);

    /* mount it if we didn't have it already */
    struct pawpaw_share* netshare;
    if (seL4_MessageInfo_get_extraCaps (reply) == 1) {
        netshare = pawpaw_share_mount (pawpaw_event_get_recv_cap ());
        pawpaw_share_set (netshare);
    } else {
        netshare = pawpaw_share_get (seL4_GetMR (0));
    }

    assert (netshare);
    pawpaw_cbuf_write (console_buffer, netshare->buf, size);

    /* check if we had waiting client (if so, dequeue) */
    if (current_reader && current_reader->current_event) {
        if (vfs_read (current_reader->current_event) ==
                PAWPAW_EVENT_NEEDS_REPLY) {

            /* send it off */
            seL4_Send (current_reader->current_event->reply_cap,
                current_reader->current_event->reply);

            /* and free */
            pawpaw_event_dispose (current_reader->current_event);
            current_reader->current_event = NULL;
        }
    }
}

/* 
 * Reads evt->args[0] amount of bytes from buffer into provided share.
 * Queues event is buffer was empty, and re-runs when buffer has data.
 */
int vfs_read (struct pawpaw_event* evt) {
    struct fhandle* fh = console_lookup_fhandle (evt->badge);
    if (!fh) {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    if (!(fh->mode & FM_READ)) {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    size_t amount = evt->args[0];
    assert (evt->share);

    /* TODO: could zero copy here... */
    int read = pawpaw_cbuf_read (console_buffer, evt->share->buf, amount);

    if (read == 0) {
        /* wait for more data */
        fh->current_event = evt;
        return PAWPAW_EVENT_HANDLED_SAVED;
    } else {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, read);
    }

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_write (struct pawpaw_event* evt) {
    struct fhandle* fh = console_lookup_fhandle (evt->badge);
    if (!fh) {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    if (!(fh->mode & FM_WRITE)) {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }
    
    assert (evt->share);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 
        pawpaw_share_attach (evt->share), 4);

    seL4_SetMR (0, NETSVC_SERVICE_SEND);
    seL4_SetMR (1, evt->share->id);
    seL4_SetMR (2, net_id);
    seL4_SetMR (3, evt->args[0]);

    /* we don't do async Send here since the Call responds quickly enough */
    seL4_Call (net_ep, msg);

    /* chill out for a few othewise we drop packets */
    usleep (CONSOLE_DELAY_TIME);

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, evt->args[0]);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_close (struct pawpaw_event* evt) {
    struct fhandle* fh = console_lookup_fhandle (evt->badge);
    if (!fh) {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* also release reader if possible */
    if (current_reader && current_reader == fh) {
        current_reader = NULL;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, 0);
    return PAWPAW_EVENT_NEEDS_REPLY;

    /* FIXME: remove from fd list + free */
}

int main (void) {
    int err;

    pawpaw_event_init ();

    service_ep = pawpaw_create_ep ();
    assert (service_ep);

    seL4_CPtr async_ep = pawpaw_create_ep_async ();
    assert (async_ep);

    err = seL4_TCB_BindAEP (PAPAYA_TCB_SLOT, async_ep);
    assert (!err);

    seL4_CPtr dev_ep = pawpaw_service_lookup (DEVSVC_SERVICE_NAME);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, service_ep);

    seL4_SetMR (0, DEVSVC_REGISTER);
    seL4_SetMR (1, DEV_CONSOLE);
    seL4_SetMR (2, DEV_PLATFORM_DEVICE);
    seL4_SetMR (3, CONSOLE_PRODUCT_ID);
    seL4_Send (dev_ep, msg);

    net_ep = pawpaw_service_lookup (NETSVC_SERVICE_NAME);
    msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, async_ep);
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER);
    seL4_SetMR (1, NETSVC_PROTOCOL_UDP);
    seL4_SetMR (2, CONSOLE_PORT);
    seL4_SetMR (3, 0);
    seL4_Call (net_ep, msg);

    /* get our network service ID (protocol + port linked to us) */
    net_id = seL4_GetMR (0);
    assert (net_id >= 0);

    /* we never need to free this */
    char* buf = malloc (sizeof (char) * CONSOLE_BUF_SIZE);
    assert (buf);
    console_buffer = pawpaw_cbuf_create (CONSOLE_BUF_SIZE, buf);

    pawpaw_event_loop (&handler_table, interrupt_handler, service_ep);

    return 0;
}
