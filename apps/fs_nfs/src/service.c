#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include "stat.h"

#include <sel4/sel4.h>
#include <syscalls.h>
#include <pawpaw.h>

#include <sos.h>
#include <vfs.h>
#include <network.h>

#include <nfs/nfs.h>
#include <ethdrivers/lwip_iface.h>
#include <autoconf.h>

#ifndef SOS_NFS_DIR
#  ifdef CONFIG_SOS_NFS_DIR
#    define SOS_NFS_DIR CONFIG_SOS_NFS_DIR
#  else
#    define SOS_NFS_DIR "/var/tftpboot/alex"
#  endif
#endif

#define FILESYSTEM_NAME     "nfs"
#define sos_usleep pawpaw_usleep

fhandle_t mnt_point = { { 0 } };

seL4_CPtr service_ep;
seL4_CPtr nfs_async_ep;

int vfs_open (struct pawpaw_event* evt);
int vfs_read (struct pawpaw_event* evt);
int vfs_write (struct pawpaw_event* evt);
int vfs_close (struct pawpaw_event* evt);
int vfs_register_cap (struct pawpaw_event* evt);
int vfs_write_offset (struct pawpaw_event* evt);
int vfs_read_offset (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   0,  0,  0   },      //              //
    {   vfs_register_cap,   0,  HANDLER_REPLY                       },  /* for async cap registration */
    {   0,  0,  0   },      //              //
    {   vfs_open,           3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },  // shareid, replyid, mode - replies with EP to file (badged version of listen cap)
    {   vfs_read,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },      //              //
    {   vfs_write,          2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },      //              //
    {   vfs_close,          0,  HANDLER_REPLY },
    {   0,  0,  0   },      /* listdir */
    {   0,  0,  0   },      /* stat */
    {   vfs_read_offset,    6,  HANDLER_AUTOMOUNT                   },
    {   vfs_write_offset,   6,  HANDLER_REPLY | HANDLER_AUTOMOUNT                   },
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers, "nfs" };

struct open_handle {
    fhandle_t fh;
    seL4_Word id;
    int offset;
    /*seL4_Word badge;*/
    int mode;

    struct open_handle* next;
};

struct open_handle* handles = NULL;
int last_handle_id = 0; /* FIXME: need bitmap */

//fhandle_t *last_handle = NULL;
struct pawpaw_event* current_event = NULL;  /* FIXME: yuck, need hashtable otherwise could race */

void vfs_create_cb (uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr) {
    if (status == NFS_OK) {
        printf ("** nfs create success\n");

        struct open_handle* handle = malloc (sizeof (struct open_handle));
        assert (handle);

        memcpy (&handle->fh, fh, sizeof (fhandle_t));
        handle->id = last_handle_id;
        handle->offset = 0;
        handle->mode = current_event->args[0];
        /*handle->badge = evt->badge;*/
        last_handle_id++;

        /* add to handle list - XXX: should be hashtable */
        handle->next = handles;
        handles = handle;

        seL4_CPtr their_cap = pawpaw_cspace_alloc_slot();
        int err = seL4_CNode_Mint (
            PAPAYA_ROOT_CNODE_SLOT, their_cap,  PAPAYA_CSPACE_DEPTH,
            PAPAYA_ROOT_CNODE_SLOT, service_ep, PAPAYA_CSPACE_DEPTH,
            seL4_AllRights, seL4_CapData_Badge_new (handle->id));
        /* XXX: hack that assumes fh->data is 32 bits - which it is */
        assert (their_cap > 0);
        assert (err == 0);

        current_event->reply = seL4_MessageInfo_new (0, 0, 1, 1);
        seL4_SetCap (0, their_cap);
        seL4_SetMR (0, 0);
    } else {
        current_event->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        printf ("** nfs create failed, had error = %d\n", status);
        seL4_SetMR (0, -1);
    }

    seL4_Send (current_event->reply_cap, current_event->reply);

    /* and free */
    pawpaw_event_dispose (current_event);
    current_event = NULL;
}

void vfs_lookup_cb (uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr) {
    if (status == NFS_OK) {
        printf ("** nfs lookup success\n");

        struct open_handle* handle = malloc (sizeof (struct open_handle));
        assert (handle);

        memcpy (&handle->fh, fh, sizeof (fhandle_t));
        handle->id = last_handle_id;
        handle->offset = 0;
        handle->mode = current_event->args[0];
        /*handle->badge = evt->badge;*/
        last_handle_id++;

        /* add to handle list - XXX: should be hashtable */
        handle->next = handles;
        handles = handle;

        seL4_CPtr their_cap = pawpaw_cspace_alloc_slot();
        int err = seL4_CNode_Mint (
            PAPAYA_ROOT_CNODE_SLOT, their_cap,  PAPAYA_CSPACE_DEPTH,
            PAPAYA_ROOT_CNODE_SLOT, service_ep, PAPAYA_CSPACE_DEPTH,
            seL4_AllRights, seL4_CapData_Badge_new (handle->id));
        /* XXX: hack that assumes fh->data is 32 bits - which it is */
        assert (their_cap > 0);
        assert (err == 0);

        current_event->reply = seL4_MessageInfo_new (0, 0, 1, 1);
        seL4_SetCap (0, their_cap);
        seL4_SetMR (0, 0);
    } else if (status == NFSERR_NOENT && current_event->args[0] & FM_WRITE) {
        /* create the file, then use that as the handle */
        /* FIXME: when we handle mutliple path depths, use parent rather than
         * root */

        printf ("** nfs lookup said file does not exist, creating...\n");
        sattr_t attributes = {
            (S_IRUSR | S_IWUSR | S_IRGRP | S_IWGRP),
            -1,             /* uid */
            -1,             /* gid */
            -1,             /* size */
            {-1},           /* atime */
            {-1}            /* mtime */
        };

        enum rpc_stat res = nfs_create (&mnt_point, current_event->share->buf, &attributes, vfs_create_cb, 0);
        printf ("create was %d\n", res);
        return;
    } else {
        current_event->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        printf ("** nfs lookup failed, had error = %d\n", status);
        seL4_SetMR (0, -1);
    }

    seL4_Send (current_event->reply_cap, current_event->reply);

    /* and free */
    pawpaw_event_dispose (current_event);
    current_event = NULL;
}

void vfs_read_cb (uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count, void* data) {
    current_event->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    if (status == NFS_OK) {
        memcpy (current_event->share->buf, data, count);
        seL4_SetMR (0, count);

        struct open_handle* handle = (struct open_handle*)token;
        handle->offset += count;
    } else {
        printf ("** nfs read failed, had error = %d\n", status);
        seL4_SetMR (0, -1);
    }

    seL4_Send (current_event->reply_cap, current_event->reply);

    /* and free */
    pawpaw_event_dispose (current_event);
    current_event = NULL;
}

void vfs_write_cb (uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count) {
    current_event->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    if (status == NFS_OK) {
        seL4_SetMR (0, count);

        struct open_handle* handle = (struct open_handle*)token;
        handle->offset += count;
    } else {
        printf ("** nfs write failed, had error = %d\n", status);
        seL4_SetMR (0, -1);
    }

    seL4_Send (current_event->reply_cap, current_event->reply);

    /* and free */
    pawpaw_event_dispose (current_event);
    current_event = NULL;
}

struct open_handle*
handle_lookup (/*seL4_Word badge, */seL4_Word id) {
    struct open_handle* handle = handles;
    while (handle) {
        if (/*handle->badge == badge && */handle->id == id) {
            return handle;
        }

        handle = handle->next;
    }

    return NULL;
}

int vfs_read (struct pawpaw_event* evt) {
    size_t amount = evt->args[0];
    assert (evt->share);

    struct open_handle* handle = handle_lookup (evt->badge);
    if (!handle) {
        printf ("nfs: could not find filehandle for given badge 0x%x\n", evt->badge);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* FIXME: check if opened for reading */

    enum rpc_stat res = nfs_read (&(handle->fh), handle->offset, amount, vfs_read_cb, (uintptr_t)handle);
    if (res != RPC_OK) {
        return PAWPAW_EVENT_HANDLED;
    }

    current_event = evt;
    //printf ("read was %d\n", res);
    return PAWPAW_EVENT_HANDLED_SAVED;
}

int vfs_write (struct pawpaw_event* evt) {
    size_t amount = evt->args[0];
    assert (evt->share);

    struct open_handle* handle = handle_lookup (evt->badge);
    if (!handle) {
        printf ("nfs: could not find filehandle for given badge 0x%x\n", evt->badge);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* FIXME: check if opened for writing */

    enum rpc_stat res = nfs_write (&(handle->fh), handle->offset, amount, evt->share->buf, vfs_write_cb, (uintptr_t)handle);
    if (res != RPC_OK) {
        return PAWPAW_EVENT_HANDLED;
    }

    current_event = evt;
    //printf ("write was %d\n", res);
    return PAWPAW_EVENT_HANDLED_SAVED;
}

int vfs_open (struct pawpaw_event* evt) {
    assert (evt->share);
    printf ("fs_nfs: want to open '%s'\n", (char*)evt->share->buf);

    /* FIXME: does not handle path names deeper than root level */

    char* fname = strdup (evt->share->buf);
    enum rpc_stat res = nfs_lookup (&mnt_point, fname, &vfs_lookup_cb, 0);
    if (res != RPC_OK) {
        return PAWPAW_EVENT_HANDLED;
    }

    printf ("lookup was %d\n", res);
    current_event = evt;

    return PAWPAW_EVENT_HANDLED_SAVED;
}

int vfs_close (struct pawpaw_event* evt) {
    struct open_handle* handle = handle_lookup (evt->badge);
    if (!handle) {
        printf ("nfs: could not find filehandle for given badge 0x%x\n", evt->badge);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* so, there's no nfs_close... */
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, 0);
    return PAWPAW_EVENT_NEEDS_REPLY;

    /* FIXME: remove from fd list + free */
    /* FIXME: what about race on current event - ie do a SEND read, then close, then input comes in */
}

static seL4_CPtr cap = 0;
int vfs_register_cap (struct pawpaw_event* evt) {
    printf ("got register\n");
    /* FIXME: regsiter cap, badge pair and lookup with 2nd argument of
     * async open */

    assert (seL4_MessageInfo_get_extraCaps (evt->msg) == 1);
    cap = pawpaw_event_get_recv_cap ();

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, 1);  /* FIXME: this should be new ID */
    return PAWPAW_EVENT_NEEDS_REPLY;
}

static seL4_CPtr lookup_cap (struct pawpaw_event* evt) {
    return cap;
}

void vfs_write_offset_cb (uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count) {
    struct pawpaw_event* evt = (struct pawpaw_event*)token;


    /* XXX: mmap should really just wait for the async return value */
    //seL4_SetMR (0, count);
    /*printf ("sending to original reply cap that we got 0x%x\n", count);
    seL4_Send (evt->reply_cap, evt->reply);*/
    
    if (status == NFS_OK) {
        seL4_SetMR (0, count);
    } else {
        printf ("** nfs write failed, had error = %d\n", status);
        seL4_SetMR (0, -1);
    }

    seL4_SetMR (1, evt->args[4]);
    printf ("sending to reply cap\n");
    seL4_Send (evt->reply_cap, evt->reply);

    // printf ("write_cb: unmounting share\n");
    // pawpaw_share_unmount (evt->share);

    /* and free */
    pawpaw_event_dispose (evt);
}

int vfs_write_offset (struct pawpaw_event *evt) {
    /*if (!lookup_cap (evt)) {
        printf ("nfs: no such reply cap registered; ignoring event\n");
        return PAWPAW_EVENT_HANDLED;
    }*/

    if (!evt->share) {
        printf ("nfs: no share\n");
        /* FIXME: should respond now we have reply cap */
        return PAWPAW_EVENT_HANDLED;
    }


    struct open_handle* handle = handle_lookup (evt->badge);
    if (!handle) {
        printf ("nfs: could not find filehandle for given badge 0x%x\n", evt->badge);
        /*evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;*/
        return PAWPAW_EVENT_HANDLED;
    }

    /* FIXME: check if opened for reading */

    seL4_Word amount = evt->args[0];
    seL4_Word file_offset = evt->args[1];
    seL4_Word buf_offset = evt->args[2];
    if (buf_offset > PAPAYA_IPC_PAGE_SIZE) {
        buf_offset = 0;
    }

    if (amount + buf_offset > PAPAYA_IPC_PAGE_SIZE) {
        amount = PAPAYA_IPC_PAGE_SIZE - buf_offset;
    }

    char* buf = evt->share->buf + buf_offset;
    printf ("nfs: ok about to write from %p for len 0x%x\n", buf, amount);
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 2);
    enum rpc_stat res = nfs_write (&(handle->fh), file_offset, amount, buf, vfs_write_offset_cb, (uintptr_t)evt);
    if (res != RPC_OK) {
        return PAWPAW_EVENT_HANDLED;
    }

    return PAWPAW_EVENT_HANDLED_SAVED;
}

void vfs_read_offset_cb (uintptr_t token, enum nfs_stat status, fattr_t *fattr, int count, void* data) {
    struct pawpaw_event* evt = (struct pawpaw_event*)token;

    if (status == NFS_OK) {
        seL4_Word buf_offset = evt->args[2];
        if (buf_offset > PAPAYA_IPC_PAGE_SIZE) {
            buf_offset = 0;
        }

        char* buf = evt->share->buf + buf_offset;

        seL4_Word amount = evt->args[0];
        
        if (amount + buf_offset > PAPAYA_IPC_PAGE_SIZE) {
            amount = PAPAYA_IPC_PAGE_SIZE - buf_offset;
        }

        /* ensure NFS doesn't cause a security vuln via buffer overflow */
        if (count > amount) {
            count = amount;
        }

        memcpy (buf, data, count);
        seL4_SetMR (0, count);
    } else {
        printf ("** nfs read failed, had error = %d\n", status);
        seL4_SetMR (0, -1);
    }

    seL4_SetMR (1, evt->args[4]);
    seL4_Send (evt->reply_cap, evt->reply);

    /* and free */
    pawpaw_event_dispose (evt);
}

int vfs_read_offset (struct pawpaw_event *evt) {
    evt->reply_cap = lookup_cap (evt);
    if (!evt->reply_cap) {
        printf ("nfs: no such reply cap registered; ignoring event\n");
        return PAWPAW_EVENT_HANDLED;
    }

    if (!evt->share) {
        printf ("nfs: no share\n");
        /* FIXME: should respond now we have reply cap */
        return PAWPAW_EVENT_HANDLED;
    }

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 2);

    struct open_handle* handle = handle_lookup (evt->badge);
    if (!handle) {
        printf ("nfs: could not find filehandle for given badge 0x%x\n", evt->badge);
        /*evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;*/
        return PAWPAW_EVENT_HANDLED;
    }

    /* FIXME: check if opened for reading */

    seL4_Word amount = evt->args[0];
    seL4_Word file_offset = evt->args[1];
    seL4_Word buf_offset = evt->args[2];
    /*if (buf_offset > PAPAYA_IPC_PAGE_SIZE) {
        buf_offset = 0;
    }*/
    
    if (amount + buf_offset > PAPAYA_IPC_PAGE_SIZE) {
        amount = PAPAYA_IPC_PAGE_SIZE - buf_offset;
    }

    printf ("nfs: ok about to read len 0x%x at offset 0x%x\n", amount, buf_offset);
    enum rpc_stat res = nfs_read (&(handle->fh), file_offset, amount, vfs_read_offset_cb, (uintptr_t)evt);
    if (res != RPC_OK) {
        return PAWPAW_EVENT_HANDLED;
    }

    return PAWPAW_EVENT_HANDLED_SAVED;
}

seL4_CPtr dev_ep = 0;

int
my_recv(void *arg, int upcb, struct pbuf *p,
    struct ip_addr *addr, u16_t port);

extern seL4_CPtr net_ep;

void interrupt_handler (struct pawpaw_event* evt) {

    /* do some shit - bring this and the code in rpc_call together */
    int id = seL4_GetMR (0);    /* FIXME: might be ORed */

    /* ok had data, go fetch it */
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);

    seL4_Word had_more = 1;
    struct pbuf* head_pbuf = NULL;

    do {
        seL4_SetMR (0, NETSVC_SERVICE_DATA);
        seL4_SetMR (1, id);
        seL4_Call (net_ep, msg);
        had_more = seL4_GetMR (2);

        int share_id = seL4_GetMR (0);
        seL4_Word size = seL4_GetMR (1);
        seL4_CPtr share_cap = pawpaw_event_get_recv_cap ();

        struct pawpaw_share* result_share = pawpaw_share_get (share_id);
        if (!result_share) {
            result_share = pawpaw_share_mount (share_cap);
            pawpaw_share_set (result_share);
        }

        assert (result_share);

        /* copy it in : FIXME - race, also fixme because we get the same share so we need to copy */
        //seL4_Word total_size = seL4_GetMR (3);
        printf ("had 0x%x bytes data\n", size);

        if (!head_pbuf) {
            //recv_pbuf = pbuf_alloc (PBUF_TRANSPORT, total_size, PBUF_REF);
            head_pbuf = pbuf_alloc (PBUF_TRANSPORT, size, PBUF_RAM);
            assert (head_pbuf);

            pbuf_take (head_pbuf, result_share->buf, size);
        } else {
            struct pbuf* p = pbuf_alloc (PBUF_TRANSPORT, size, PBUF_RAM);
            assert (p);
            pbuf_take (p, result_share->buf, size);

            pbuf_cat (p, head_pbuf);
            head_pbuf = p;
        }
    } while (had_more);


    /* create pbuf: FIXME maybe need a local copy rather than ref? */
    //head_pbuf->payload = result_share->buf;
    //assert (head_pbuf->tot_len == size);

    printf ("total size was: 0x%x\n", head_pbuf->tot_len);

    /* handle it */
    //debug ("calling recv function\n");
    /* FIXME: unmount share after recv, also MAN WTF RETURN CODES */
    my_recv (NULL, id, head_pbuf, NULL, 0);

    pbuf_free (head_pbuf);
}

int main (void) {
    int err;
    struct ip_addr gateway;

    pawpaw_event_init ();

    /* async ep for network stuff */
    nfs_async_ep = pawpaw_create_ep_async ();
    assert (nfs_async_ep);

    err = seL4_TCB_BindAEP (PAPAYA_TCB_SLOT, nfs_async_ep);
    assert (!err);

    /* EP we give to people if they should talk to us */
    service_ep = pawpaw_create_ep ();
    assert (service_ep);

    /* init NFS */
    ipaddr_aton (CONFIG_SOS_GATEWAY, &gateway);
    if (nfs_init (&gateway) != RPC_OK) {
        return -1;
    }

    //nfs_print_exports ();

    /* TODO: don't just mount the NFS dir, let the user pick! */
    if ((err = nfs_mount (SOS_NFS_DIR, &mnt_point))){
        return -1;
    }

    /* now that we're "setup", register this filesystem with the VFS */
    seL4_CPtr vfs_ep = pawpaw_service_lookup (VFSSVC_SERVICE_NAME);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 2);
    struct pawpaw_share *newshare = pawpaw_share_new ();
    assert (newshare);
    pawpaw_share_set (newshare);

    strcpy (newshare->buf, FILESYSTEM_NAME);

    seL4_SetMR (0, VFS_REGISTER_INFO);
    seL4_SetMR (1, newshare->id);
    pawpaw_share_attach (newshare);
    seL4_Call (vfs_ep, msg);

    /* now attach that cap - people can now mount us */
    msg = seL4_MessageInfo_new (0, 0, 1, 1);
    seL4_SetMR (0, VFS_REGISTER_CAP);
    seL4_SetCap (0, service_ep);
    seL4_Call (vfs_ep, msg);

    /* setup done, now listen to VFS or other people we've given our EP to */
    pawpaw_event_loop (&handler_table, interrupt_handler, service_ep);

    return 0;
}

/****************************
 *** Sync library support ***
 ****************************/

/* FIXME: this needs to go - only used by
 * libnfs' rpc.c for checking if RPCs have timed out
 *
 * a much better way would be to get dev_timer to call
 * us back after 100 ms or whatever 
 *
 * DOUBLE FIXME: we don't even do that(!) so packets might get lost
 * but nice and easy now
 */
struct sync_ep_node {
    seL4_CPtr cap;
    struct sync_ep_node* next;
};

struct sync_ep_node* sync_ep_list = NULL;

/* Provide an endpoint ready for use */
void* sync_new_ep (seL4_CPtr* ep_cap) {
    struct sync_ep_node *epn = sync_ep_list;

    if (epn) {
        /* Use endpoint from the pool */
        sync_ep_list = epn->next;
    } else {
        /* Pool is dry... Make another endpoint */
        epn = (struct sync_ep_node*)malloc (sizeof (*epn));
        if (epn == NULL){
            return NULL;
        }
        epn->cap = pawpaw_create_ep_async ();
        if (!epn->cap) {
            return NULL;
        }
    }

    *ep_cap = epn->cap;
    return epn;
}

/* Don't free, just recycle it to our pool of end points */
void sync_free_ep (void* _epn) {
    struct sync_ep_node *epn = (struct sync_ep_node*)_epn;
    assert (epn);

    /* Add to our pool */
    epn->next = sync_ep_list;
    sync_ep_list = epn;
}
