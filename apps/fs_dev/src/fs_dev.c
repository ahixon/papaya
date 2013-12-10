#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>
#include <pawpaw.h>

#include <vfs.h>
#include <sos.h>
#include <device.h>

#include "fs_dev.h"

/* find the relevant device to open, and then forward the open request 
 * off to it, if successful */
int vfs_open (struct pawpaw_event* evt) {
    if (!evt->share) {
        printf ("fs_dev: no share\n");
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

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
        printf ("fs_dev: no file called '%s'\n", (char*)evt->share->buf);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    seL4_MessageInfo_t underlying_msg = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, VFS_OPEN);

    /* can't execute devices */
    if (evt->args[0] & FM_EXEC) {
        printf ("fs_dev: execute invalid\n");
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    seL4_SetMR (1, evt->args[0]);   /* file mode */
    seL4_SetMR (2, evt->args[1]);   /* owner badge */

    /* XXX: should be Send - we don't want to (possibly) wait here forever */
    seL4_MessageInfo_t reply = seL4_Call (ret, underlying_msg);

    /* and tell VFS layer */
    if (seL4_MessageInfo_get_extraCaps (reply) == 1) {
        seL4_CPtr dev_fd_cap = pawpaw_event_get_recv_cap ();

        evt->reply = seL4_MessageInfo_new (0, 0, 1, 1);
        seL4_SetCap (0, dev_fd_cap);
        seL4_SetMR (0, 0);
    } else {
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
    }

    evt->flags |= PAWPAW_EVENT_NO_UNMOUNT;  /* for VFS */
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_listdir (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    if (!evt->share) {
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    int i = 0;
    int read = 0;
    struct ventry* entry = entries;
    while (entry) {
        if (i == evt->args[0]) {
            read = strlen (entry->name);
            if (read > evt->args[1]) {
                read = evt->args[1];
            }

            memcpy (evt->share->buf, entry->name, read);
            break;
        }

        i++;
        entry = entry->next;
    }

    seL4_SetMR (0, read);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_stat (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    if (!evt->share) {
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    char* cmp = evt->share->buf;

    int success = -1;
    struct ventry* entry = entries;
    while (entry) {
        if (strcmp (cmp, entry->name) == 0) {
            stat_t stat = {
                ST_SPECIAL,
                /* XXX: strictly speaking, should interrogate devices
                 * to see what they support (ie read, write, readwrite), but 
                 * use this as a safe default */
                FM_READ | FM_WRITE,
                0,  /* size */
                0,  /* creation time */
                0,  /* access time */
            };

            memcpy (evt->share->buf, &stat, sizeof (stat_t));
            success = 0;
            break;
        }

        entry = entry->next;
    }

    seL4_SetMR (0, success);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

seL4_CPtr dev_ep = 0;

void interrupt_handler (struct pawpaw_event* evt) {
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, DEVSVC_GET_INFO);
    seL4_SetMR (1, evt->args[0]);

    seL4_MessageInfo_t reply = seL4_Call (dev_ep, msg);

    /* allocate it */
    struct ventry* ve = malloc (sizeof (struct ventry));
    assert (ve);

    seL4_Word type = seL4_GetMR (0);

    /* FIXME: only supports one of each type */
    if (type == DEV_CONSOLE) {
        ve->name = "console";
    } else if (type == DEV_TIMER) {
        ve->name = "timer";
    } else {
        ve->name = "unknown";
    }

    assert (seL4_MessageInfo_get_extraCaps (reply) == 1);
    ve->vnode = pawpaw_event_get_recv_cap ();
    ve->next = entries;
    entries = ve;
}

int main (void) {
    int err;
    seL4_MessageInfo_t msg;

    pawpaw_event_init ();

    seL4_CPtr async_ep = pawpaw_create_ep_async ();
    assert (async_ep);

    err = seL4_TCB_BindAEP (PAPAYA_TCB_SLOT, async_ep);
    assert (!err);

    /* EP we give to people if they should talk to us */
    service_ep = pawpaw_create_ep ();
    assert (service_ep);
    
    /* ask the device service to notify us when a device is added/removed */
    dev_ep = pawpaw_service_lookup (DEVSVC_SERVICE_NAME);
    assert (dev_ep);

    msg = seL4_MessageInfo_new (0, 0, 1, 1);
    seL4_SetMR (0, DEVSVC_LISTEN_CHANGES);
    seL4_SetMR (1, 0);  // all

    seL4_SetCap (0, async_ep);
    seL4_Call (dev_ep, msg);

    /* register this filesystem with the VFS */
    seL4_CPtr vfs_ep = pawpaw_service_lookup (VFSSVC_SERVICE_NAME);

    msg = seL4_MessageInfo_new (0, 0, 1, 2);
    struct pawpaw_share *newshare = pawpaw_share_new ();
    assert (newshare);

    /* register it - kinda important so we can mount without cap just id */
    pawpaw_share_set (newshare);

    strcpy (newshare->buf, FILESYSTEM_NAME);

    seL4_SetMR (0, VFS_REGISTER_INFO);
    seL4_SetMR (1, newshare->id);
    pawpaw_share_attach (newshare);
    seL4_Call (vfs_ep, msg);

    msg = seL4_MessageInfo_new (0, 0, 1, 1);
    seL4_SetMR (0, VFS_REGISTER_CAP);
    seL4_SetCap (0, service_ep);

    seL4_Call (vfs_ep, msg);

    /* setup done, now listen to VFS or other people we've given our EP to */
    pawpaw_event_loop (&handler_table, interrupt_handler, service_ep);

    return 0;
}