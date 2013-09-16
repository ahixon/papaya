#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>
#include <pawpaw.h>

#include <sos.h>

//#define VFS_MOUNT               75
#define DEV_LISTEN_CHANGES      20

seL4_CPtr service_ep;

void hacky_mount (void) {
    /* XXX: someone else should be doing this.. */
    seL4_CPtr vfs_ep = pawpaw_service_lookup("sys.vfs");
    assert (vfs_ep);    // XXX: assuming should've started by now
    printf ("OK ABOUT TO ASK FOR 4 CANS\n");
    struct pawpaw_can* can = pawpaw_can_negotiate (vfs_ep, 4);
    assert (can);

    char* mount_info = pawpaw_bean_get (can, 0);
    strcpy (mount_info, "/dev");

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 3);
    seL4_SetCap (0, service_ep);

    seL4_SetMR (0, VFS_MOUNT);
    seL4_SetMR (1, 0);
    seL4_SetMR (2, 0);  // num args
    seL4_Call (vfs_ep, msg);
}

int main(void) {
    int err;
    seL4_MessageInfo_t msg;

    seL4_CPtr dev_ep = 0;
    while (!dev_ep) {
        dev_ep = pawpaw_service_lookup ("sys.dev");
        if (!dev_ep) {
            seL4_Yield();
        }
    }

    /*seL4_CPtr dev_ep = pawpaw_cspace_alloc_slot ();
    seL4_SetCapReceivePath (4, dev_ep, CSPACE_DEPTH);*/

    printf ("fs_dev: creating async EP\n");
    seL4_CPtr async_ep = pawpaw_create_ep_async();
    assert (async_ep);

    printf ("fs_dev: binding TCB and async EP\n");
    err = seL4_TCB_BindAEP (PAPAYA_TCB_SLOT, async_ep);
    assert (!err);

    printf ("fs_dev: creating sync EP\n");
    service_ep = pawpaw_create_ep ();
    assert (service_ep);
    
    /* OK ask the device service to notify us when a device is added/removed */
    printf ("fs_dev: asking device manager to tell us about all device changes\n");
    msg = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR (0, DEV_LISTEN_CHANGES);
    seL4_SetMR (1, 0);  // all

    seL4_SetCap (0, async_ep);
    seL4_Call (dev_ep, msg);

    hacky_mount ();

    printf ("fs_dev: setup done\n");
    while (1) {
        seL4_Word badge;
        msg = seL4_Wait(service_ep, &badge);

        uint32_t label = seL4_MessageInfo_get_label(msg);
        printf ("** fs_dev ** received message from %x with label %d and length %d\n", badge, label, seL4_MessageInfo_get_length (msg));

        if (seL4_GetMR (0) == VFS_OPEN) {
            printf ("hello for the second last open call\n");
        } else if (seL4_GetMR (0) == SYSCALL_CAN_NEGOTIATE) {
            printf ("FS_DEV: CREATING FOR %d\n", badge);
            struct pawpaw_can* can = pawpaw_can_allocate (seL4_GetMR (2));

            seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 2);
            seL4_SetMR (0, 16);
            seL4_SetMR (1, (seL4_Word)can);

            //seL4_Send (reply_cap, reply);
            seL4_Reply (reply);

        }

        /*seL4_MessageInfo_t newmsg = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);
        seL4_Reply (newmsg);*/
    }
}
