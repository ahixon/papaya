#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>

#include <pawpaw.h>

#include <syscalls.h>
#include <network.h>
#include <sos.h>
#include <vfs.h>

#define DEV_REGISTER            21

#define CONSOLE_BUF_SIZE        1024
#define CONSOLE_PORT            26706
//#define NETSVC_SERVICE_DATA     3

struct pawpaw_cbuf* console_buffer;
seL4_CPtr service_ep = 0;
seL4_CPtr net_ep = 0;
int net_id = -1;

int vfs_open (struct pawpaw_event* evt);
int vfs_read (struct pawpaw_event* evt);
int vfs_write (struct pawpaw_event* evt);
int vfs_close (struct pawpaw_event* evt);

void interrupt_handler (struct pawpaw_event* evt);
int interrupt_handler_2 (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   0,  0,  0   },      //   RESERVED   //
    {   0,  0,  0   },      //   RESERVED   //
    {   0,  0,  0   },      //              //
    {   vfs_open,           2,  HANDLER_REPLY },    // mode + badge, replies with EP to file (badged version of listen cap)
    {   vfs_read,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT },    // num bytes
    {   vfs_write,          2,  HANDLER_REPLY | HANDLER_AUTOMOUNT },    // num bytes
    {   vfs_close,          0,  HANDLER_REPLY },
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers, "console" };

struct fhandle* current_reader = NULL;

struct fhandle {
    //struct pawpaw_share* share;
    struct pawpaw_event* current_event;
    seL4_Word id;
    int mode;
    //seL4_CPtr cap;

    struct fhandle* next;
};

struct fhandle* open_list = NULL;

struct fhandle* lookup_fhandle (seL4_Word badge) {
    struct fhandle* h = open_list;
    while (h) {
        if (h->id == badge) {
            return h;
        }

        h = h->next;
    }

    return NULL;
}

int last_opened = 1;

int vfs_open (struct pawpaw_event* evt) {
    if (evt->args[0] & FM_READ) {
        /* FIXME: this sucks */
        if (current_reader) {
            printf ("console: already opened for reading\n");
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

    printf ("console: open success\n");
    printf ("opened for reading? %d\n", evt->args[0] & FM_READ);
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
    //printf ("console: net had data for us, fetching...\n");

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, NETSVC_SERVICE_DATA);
    seL4_SetMR (1, net_id);
    seL4_MessageInfo_t reply = seL4_Call (net_ep, msg);

    seL4_Word size = seL4_GetMR (1);

    /* mount it if we didn't have it already */
    struct pawpaw_share* netshare;
    if (seL4_MessageInfo_get_extraCaps (reply) == 1) {
        printf ("console: using receive cap\n");
        netshare = pawpaw_share_mount (pawpaw_event_get_recv_cap ());
        pawpaw_share_set (netshare);
    } else {
        netshare = pawpaw_share_get (seL4_GetMR (0));
        printf ("console: mounting ID 0x%x (was 0x%x bytes)\n", seL4_GetMR (0), size);
    }

    assert (netshare);
    printf ("console: actually had ID 0x%x\n", netshare->id);

    //printf ("console: writing '%s' to buffer\n", netshare->buf);
    pawpaw_cbuf_write (console_buffer, netshare->buf, size);

    /* FIXME: unmount? probably not since internal buffer */
    //pawpaw_share_unmount (netshare_;)

    /* check if we had waiting client */
    if (current_reader && current_reader->current_event) {
        if (vfs_read (current_reader->current_event) == PAWPAW_EVENT_NEEDS_REPLY) {
            /* send it off */
            //printf ("console: sending queued event to client\n");
            seL4_Send (current_reader->current_event->reply_cap, current_reader->current_event->reply);

            /* and free */
            pawpaw_event_dispose (current_reader->current_event);
            current_reader->current_event = NULL;
        }
    } else {
        printf ("!!! no read queued, leaving in buffer\n");
    }
}

/* 
 * Reads evt->args[0] amount of bytes from buffer into provided share.
 * Queues event is buffer was empty, and re-runs when buffer has data.
 */
int vfs_read (struct pawpaw_event* evt) {
    struct fhandle* fh = lookup_fhandle (evt->badge);
    if (!fh) {
        printf ("console: did not find handle for badge 0x%x\n", evt->badge);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    if (!(fh->mode & FM_READ)) {
        printf ("console: tried to read on file not opened for reading\n");
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    size_t amount = evt->args[0];
    assert (evt->share);

    /* FIXME: could zero copy, but lazy */
    //printf ("console: wanted to read 0x%x bytes\n", amount);
    //printf ("console: buffer currently has %d, trying to read into %p\n", pawpaw_cbuf_count (console_buffer), evt->share->buf);
    int read = pawpaw_cbuf_read (console_buffer, evt->share->buf, amount);

    if (read == 0) {
        //printf ("console: buffer was empty, waiting for interrupt..\n");
        /* wait for more data */

        fh->current_event = evt;
        return PAWPAW_EVENT_HANDLED_SAVED;
    } else {
        //printf ("console: managed to read 0x%x bytes, sending back '%s'\n", read, evt->share->buf);

        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        //seL4_SetMR (0, evt->share->id);
        seL4_SetMR (0, read);
    }

    //evt->flags |= PAWPAW_EVENT_UNMOUNT;
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_write (struct pawpaw_event* evt) {
    struct fhandle* fh = lookup_fhandle (evt->badge);
    if (!fh) {
        printf ("console: did not find handle for badge 0x%x\n", evt->badge);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    if (!(fh->mode & FM_WRITE)) {
        printf ("console: tried to write on file not opened for writing\n");
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }
    
    assert (evt->share);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, evt->share->cap);
    seL4_SetMR (0, NETSVC_SERVICE_SEND);
    seL4_SetMR (1, evt->share->id);
    seL4_SetMR (2, net_id);
    seL4_SetMR (3, evt->args[0]);

    /* FIXME: should be write? or not really in this case since it's fast enough */
    seL4_Call (net_ep, msg);

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, evt->args[0]);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_close (struct pawpaw_event* evt) {
    struct fhandle* fh = lookup_fhandle (evt->badge);
    if (!fh) {
        printf ("console: did not find handle for badge 0x%x\n", evt->badge);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    printf ("console: closing fd 0x%x\n", evt->badge);

    if (current_reader && current_reader == fh) {
        printf ("console: also released reader\n");
        current_reader = NULL;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, 0);
    return PAWPAW_EVENT_NEEDS_REPLY;

    /* FIXME: remove from fd list + free */
    /* FIXME: what about race on current event - ie do a SEND read, then close, then input comes in */
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

    seL4_CPtr dev_ep = pawpaw_service_lookup ("svc_dev");

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetMR (0, DEV_REGISTER);
    
    seL4_SetCap (0, service_ep);

    seL4_SetMR (1, 0);              // type = console
    seL4_SetMR (2, 0);              // bus = platform device
    seL4_SetMR (3, 1337);           // product ID = 1337 lol??

    /* FIXME: do we REALLY need Call? at the moment, no lol */
    printf ("console: registering with svc_dev\n");
    seL4_Send (dev_ep, msg);    /*  FIXME: should eventually be Call, but at the
                                    moment svc_dev never responds so don't change
                                    it unless you want to spend a while debugging */

    net_ep = pawpaw_service_lookup ("svc_net");
    msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, async_ep);
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER);
    seL4_SetMR (1, NETSVC_PROTOCOL_UDP);
    seL4_SetMR (2, CONSOLE_PORT);
    seL4_SetMR (3, 0);

    printf ("console: registering with svc_net\n");
    seL4_Call (net_ep, msg);
    net_id = seL4_GetMR (0);
    assert (net_id >= 0);

    /* we never need to free this */
    char* buf = malloc (sizeof (char) * CONSOLE_BUF_SIZE);
    assert (buf);

    console_buffer = pawpaw_cbuf_create (CONSOLE_BUF_SIZE, buf);

    pawpaw_event_loop (&handler_table, interrupt_handler, service_ep);

    return 0;
}
