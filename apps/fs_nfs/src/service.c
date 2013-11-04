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

//#define VFS_MOUNT               75
#define DEV_LISTEN_CHANGES      20
#define DEV_GET_INFO            25

#define FILESYSTEM_NAME     "nfs"

seL4_CPtr service_ep;

struct ventry {
    char* name;
    seL4_CPtr vnode;
    int writing;

    struct ventry* next;
};

struct ventry* entries;

int vfs_open (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   0,  0,  0   },      //              //
    {   0,  0,  0   },      //   RESERVED   //
    {   0,  0,  0   },      //              //
    {   vfs_open,           3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },  // shareid, replyid, mode - replies with EP to file (badged version of listen cap)
    {   0,  0,  0   },      //              //
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers };

int vfs_open (struct pawpaw_event* evt) {
    assert (evt->share);
    printf ("fs_dev: want to open '%s'\n", evt->share->buf);

    /*assert (seL4_MessageInfo_get_extraCaps (evt->msg) == 1);
    seL4_CPtr requestor = pawpaw_event_get_recv_cap ();*/

    seL4_CPtr ret = 0;
    struct ventry* entry = entries;

    while (entry) {
        if (strcmp (entry->name, evt->share->buf) == 0) {
            ret = entry->vnode;
            break;
        }

        entry = entry->next;
    }

    if (!ret) {
        printf ("fs_dev: no such file\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    seL4_MessageInfo_t underlying_msg = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, VFS_OPEN);
    if (evt->args[0] & FM_EXEC) {
        printf ("fs_dev: can't execute devices?\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    seL4_SetMR (1, evt->args[0]);   /* file mode */
    seL4_SetMR (2, evt->args[1]);   /* owner badge */

    /* could be Call */
    printf ("calling %d\n", ret);
    seL4_MessageInfo_t reply = seL4_Call (ret, underlying_msg);

    /* attach the FD cap to our reply */
    /*seL4_MessageInfo_t requestor_msg = seL4_MessageInfo_new (0, 0, 1, 1);
    seL4_SetMR (0, 0);

    seL4_CPtr dev_fd_cap = pawpaw_event_get_recv_cap ();
    printf ("OK sending back to %d\n", requestor);
    seL4_SetCap (0, dev_fd_cap);

    seL4_Send (requestor, requestor_msg);*/

    //printf ("fs_dev: finally telling VFS how we went\n");

    /* and tell the VFS layer how we went (for caching) */
    /*seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);
    if (ret) {
        seL4_SetMR (0, 0);  // file OK
    } else {
        seL4_SetMR (0, 1);  // file not OK
    }

    seL4_Reply (reply);
    pawpaw_cspace_free_slot (resp_cap);*/

    /* and tell VFS layer */
    assert (seL4_MessageInfo_get_extraCaps (reply) == 1);
    seL4_CPtr dev_fd_cap = pawpaw_event_get_recv_cap ();

    evt->reply = seL4_MessageInfo_new (0, 0, 1, 1);
    seL4_SetCap (0, dev_fd_cap);
    seL4_SetMR (0, 0);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

seL4_CPtr dev_ep = 0;

void interrupt_handler (struct pawpaw_event* evt) {
    printf ("fs_nfs: got interrupt\n");
}

int main (void) {
    int err;
    seL4_MessageInfo_t msg;

    pawpaw_event_init ();

    /* async ep for network stuff */
    seL4_CPtr async_ep = pawpaw_create_ep_async ();
    assert (async_ep);

    err = seL4_TCB_BindAEP (PAPAYA_TCB_SLOT, async_ep);
    assert (!err);

    /* EP we give to people if they should talk to us */
    service_ep = pawpaw_create_ep ();
    assert (service_ep);

    /* init NFS */
    struct ip_addr gateway; /* TODO: should prime ARP table? */ 
    ipaddr_aton (CONFIG_SOS_GATEWAY,      &gw);

    if (nfs_init (&gw)) {
        nfs_print_exports ();
        return 1;
    } else {
        printf ("NFS init failed\n");
        return 0;
    }
#if 0

    /* now that we're "setup", register this filesystem with the VFS */
    seL4_CPtr vfs_ep = pawpaw_service_lookup ("svc_vfs");

    /* first, register it - kinda important so we can mount without cap just id */
    msg = seL4_MessageInfo_new (0, 0, 1, 2);
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
    newshare->buf = '\0';
    
    msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, VFS_MOUNT);
    seL4_SetMR (1, newshare->id);

    seL4_Call (vfs_ep, msg);

    /* setup done, now listen to VFS or other people we've given our EP to */
    pawpaw_event_loop (&handler_table, interrupt_handler, service_ep);

    return 0;
#endif
}