#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

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

//#define VFS_MOUNT               75
#define DEV_LISTEN_CHANGES      20
#define DEV_GET_INFO            25

#define FILESYSTEM_NAME     "nfs"
#define sos_usleep pawpaw_usleep

fhandle_t mnt_point = { { 0 } };

seL4_CPtr service_ep;
seL4_CPtr nfs_async_ep;

struct ventry {
    char* name;
    seL4_CPtr vnode;
    int writing;

    struct ventry* next;
};

struct ventry* entries;

int vfs_open (struct pawpaw_event* evt);
int vfs_read (struct pawpaw_event* evt);
int vfs_close (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   0,  0,  0   },      //              //
    {   0,  0,  0   },      //   RESERVED   //
    {   0,  0,  0   },      //              //
    {   vfs_open,           3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },  // shareid, replyid, mode - replies with EP to file (badged version of listen cap)
    {   vfs_read,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },      //              //
    {   0,  0,  0   },      //              //
    {   vfs_close,          0,  HANDLER_REPLY },
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers, "nfs" };

struct open_handle {
    fhandle_t fh;
    seL4_Word id;
    int offset;
    /*seL4_Word badge;*/

    struct open_handle* next;
};

struct open_handle* handles = NULL;
int last_handle_id = 0; /* FIXME: need bitmap */

//fhandle_t *last_handle = NULL;
struct pawpaw_event* current_event = NULL;  /* FIXME: yuck, need hashtable otherwise could race */

void vfs_lookup_cb (uintptr_t token, enum nfs_stat status, fhandle_t *fh, fattr_t *fattr) {
    if (status == NFS_OK) {
        printf ("** nfs lookup success\n");

        struct open_handle* handle = malloc (sizeof (struct open_handle));
        assert (handle);

        memcpy (&handle->fh, fh, sizeof (fhandle_t));
        handle->id = last_handle_id;
        handle->offset = 0;
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

    enum rpc_stat res = nfs_read (&(handle->fh), handle->offset, amount, vfs_read_cb, handle);
    current_event = evt;
    printf ("read was %d\n", res);
    return PAWPAW_EVENT_HANDLED_SAVED;
}

int vfs_open (struct pawpaw_event* evt) {
    assert (evt->share);
    printf ("fs_nfs: want to open '%s'\n", (char*)evt->share->buf);

    char* fname = strdup (evt->share->buf);
    printf ("fs_nfs: using mount point 0x%x\n", mnt_point.data);
    enum rpc_stat res = nfs_lookup (&mnt_point, fname, &vfs_lookup_cb, 0);
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

seL4_CPtr dev_ep = 0;

int
my_recv(void *arg, int upcb, struct pbuf *p,
    struct ip_addr *addr, u16_t port);

#define debug(x...) printf( x )
extern seL4_CPtr net_ep;

void interrupt_handler (struct pawpaw_event* evt) {
    printf ("fs_nfs: got interrupt\n");

    /* do some shit - bring this and the code in rpc_call together */
    int id = seL4_GetMR (0);    /* FIXME: might be ORed */
    debug ("got reply on conn %d\n", id);

    /* ok had data, go fetch it */
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, NETSVC_SERVICE_DATA);
    seL4_SetMR (1, id);

    debug ("asking for data for connection %d\n", id);
    seL4_Call (net_ep, msg);
    seL4_Word size = seL4_GetMR(1);
    debug ("had 0x%x bytes data\n", size);

    int share_id = seL4_GetMR (0);
    seL4_CPtr share_cap = pawpaw_event_get_recv_cap ();

    struct pawpaw_share* result_share = pawpaw_share_get (share_id);
    if (!result_share) {
        result_share = pawpaw_share_mount (share_cap);
        pawpaw_share_set (result_share);
    }

    assert (result_share);

    /* create pbuf: FIXME maybe need a local copy rather than ref? */
    struct pbuf* recv_pbuf = pbuf_alloc (PBUF_TRANSPORT, size, PBUF_REF);
    assert (recv_pbuf);
    recv_pbuf->payload = result_share->buf;
    assert (recv_pbuf->tot_len == size);

    /* handle it */
    //debug ("calling recv function\n");
    /* FIXME: unmount share after recv */
    my_recv (NULL, id, recv_pbuf, NULL, 0);
}

int main (void) {
    int err;

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
    struct ip_addr gateway; /* TODO: should prime ARP table? */ 
    ipaddr_aton (CONFIG_SOS_GATEWAY,      &gateway);

    if (nfs_init (&gateway) == RPC_OK) {
        nfs_print_exports ();
    } else {
        printf ("NFS init failed\n");
    }

    /* TODO: don't just mount the NFS dir, let the user pick! */
    if ((err = nfs_mount(SOS_NFS_DIR, &mnt_point))){
        printf("Error mounting path '%s', err = %d!\n", SOS_NFS_DIR, err);
        return 1;
    }

    /* now that we're "setup", register this filesystem with the VFS */
    seL4_CPtr vfs_ep = pawpaw_service_lookup ("svc_vfs");

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 2);
    struct pawpaw_share *newshare = pawpaw_share_new ();
    assert (newshare);

    pawpaw_share_set (newshare);

    strcpy (newshare->buf, FILESYSTEM_NAME);

    seL4_SetMR (0, VFS_REGISTER_INFO);
    seL4_SetMR (1, newshare->id);
    pawpaw_share_attach (newshare);
    seL4_Call (vfs_ep, msg);    /* FIXME: would we ever need call? otherwise this is OK :) */

    /* now attach that cap - people can now mount us */
    msg = seL4_MessageInfo_new (0, 0, 1, 1);
    seL4_SetMR (0, VFS_REGISTER_CAP);
    seL4_SetCap (0, service_ep);

    seL4_Call (vfs_ep, msg);    /* FIXME: would we ever need call? otherwise this is OK :) */

    /* XXX: mount device fs to / - svc_init should do this instead */
    strcpy (newshare->buf, "/");
    strcpy ((newshare->buf) + 2, FILESYSTEM_NAME);
    
    msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, VFS_MOUNT);
    seL4_SetMR (1, newshare->id);

    seL4_Call (vfs_ep, msg);

    /* setup done, now listen to VFS or other people we've given our EP to */
    printf ("nfs: started\n");
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
 * us back after 100 ms or whatever */
struct sync_ep_node {
    seL4_CPtr cap;
    //seL4_Word paddr;
    struct sync_ep_node* next;
};

struct sync_ep_node* sync_ep_list = NULL;

/* Provide an endpoint ready for use */
void * 
sync_new_ep(seL4_CPtr* ep_cap){
    // printf ("Asked for new sync EP\n");
    struct sync_ep_node *epn = sync_ep_list;
    if(epn){
        /* Use endpoint from the pool */
        sync_ep_list = epn->next;

    }else{
        /* Pool is dry... Make another endpoint */
        epn = (struct sync_ep_node*)malloc(sizeof(*epn));
        if(epn == NULL){
            printf ("%s: malloc failed\n", __FUNCTION__);
            return NULL;
        }
        epn->cap = pawpaw_create_ep_async ();
        if (!epn->cap) {
            printf ("%s: async EP creation failed\n", __FUNCTION__);
            return NULL;
        }
    }
    *ep_cap = epn->cap;
    //printf ("%s: creation succeeded @ %p\n", __FUNCTION__, epn);
    return epn;
}

/* Don't free, just recycle it to our pool of end points */
void 
sync_free_ep(void* _epn){
    // printf ("%s: freeing EP %p\n", __FUNCTION__, _epn);
    struct sync_ep_node *epn = (struct sync_ep_node*)_epn;
    assert(epn);
    /* Add to our pool */
    epn->next = sync_ep_list;
    sync_ep_list = epn;
}
