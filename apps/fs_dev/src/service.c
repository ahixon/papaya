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

seL4_CPtr service_ep;

struct ventry {
    char* name;
    seL4_CPtr vnode;

    struct ventry* next;
};

struct ventry* entries;

char* svc_names[2] = {"svc_dev", "svc_vfs"};

int main(void) {
    int err;
    seL4_MessageInfo_t msg;

    seL4_CPtr dev_ep = pawpaw_service_lookup (svc_names[0], true);

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

    seL4_CPtr resp_cap = pawpaw_cspace_alloc_slot ();
    assert (resp_cap);
    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, resp_cap, PAPAYA_CSPACE_DEPTH);
    
    /* OK ask the device service to notify us when a device is added/removed */
    printf ("fs_dev: asking device manager to tell us about all device changes\n");
    msg = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR (0, DEV_LISTEN_CHANGES);
    seL4_SetMR (1, 0);  // all

    seL4_SetCap (0, async_ep);
    seL4_Call (dev_ep, msg);

    /* and register this filesystem - requires 2x syscalls to setup and 1x context switch */
    seL4_CPtr vfs_ep = pawpaw_service_lookup (svc_names[1], true);
    sbuf_t vfs_buf = pawpaw_sbuf_create (2);

    msg = seL4_MessageInfo_new (0, 0, 1, 3);

    seL4_SetCap (0, pawpaw_sbuf_get_cap (vfs_buf));
    int slot = pawpaw_sbuf_slot_next (vfs_buf);
    char* fs_name = pawpaw_sbuf_slot_get (vfs_buf, slot);
    printf ("copying into %p\n", fs_name);
    strcpy (fs_name, "dev");    /* you can stick other stuff in here too I guess */
    printf ("see? %s\n", fs_name);

    printf ("fs_dev: calling VFS register\n");
    seL4_SetMR (0, VFS_REGISTER);
    seL4_SetMR (1, pawpaw_sbuf_get_id (vfs_buf));
    seL4_SetMR (2, (unsigned int)slot);
    seL4_Send (vfs_ep, msg);    /* FIXME: would we ever need call? otherwise this is OK :) */

    printf ("fs_dev: linking cap (hopefully)\n");
    msg = seL4_MessageInfo_new (0, 0, 1, 1);
    seL4_SetCap (0, service_ep);
    seL4_SetMR (0, VFS_LINK_CAP);
    seL4_Send (vfs_ep, msg);

    printf ("fs_dev: setup done\n");
    while (1) {
        seL4_Word badge;
        msg = seL4_Wait(service_ep, &badge);

        uint32_t label = seL4_MessageInfo_get_label(msg);
        printf ("** fs_dev ** received message from %x with label %d and length %d\n", badge, label, seL4_MessageInfo_get_length (msg));

        if (seL4_GetMR (0) == VFS_OPEN) {
            unsigned int slot = seL4_GetMR(2);
            printf ("wut\n");
            sbuf_t fs_buf = vfs_buf;
            //sbuf_t fs_buf = pawpaw_sbuf_fetch (seL4_GetMR (1));
            //sbuf_t fs_buf = pawpaw_sbuf_fetch (pawpaw_sbuf_get_id (vfs_buf));
            if (!fs_buf) {
                printf ("invalid buf id\n");
                continue;
            }

            char* fs_filename = pawpaw_sbuf_slot_get (fs_buf, slot);
            printf ("HAD FILENAME %s in slot %d (vaddr %p)\n", fs_filename, slot, fs_filename);
            #if 0
            seL4_CPtr ret = 0;
            struct ventry* entry = entries;

            /* looks up the container in our local cache to see if we've already mapped it, otherwise ask the root server to map us in */
            container_t container = pawpaw_mbox_container_get (badge);
            if (!container) {
                container = pawpaw_mbox_container_create (badge, resp_cap);
            }

            struct pawpaw_can* can = pawpaw_can_fetch (badge);
            printf ("GOT VFS OPEN ON can %p\n", can);
            char* name = pawpaw_bean_get (can, seL4_GetMR (1));

            while (entry) {
                if (strcmp (entry->name, name) == 0) {
                    ret = entry->vnode;
                    break;
                }

                entry = entry->next;
            }

            if (ret) {
                seL4_MessageInfo_t underlying_msg = seL4_MessageInfo_new (0, 0, 1, 1);
                seL4_SetCap (0, resp_cap);
                seL4_SetMR (0, VFS_OPEN);
                seL4_Send (ret, underlying_msg);
            }

            /* and tell the VFS layer how we went (for caching) */
            seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);
            if (ret) {
                seL4_SetMR (0, 0);  // file OK
            } else {
                seL4_SetMR (0, 1);  // file not OK
            }

            //seL4_Send (reply_cap, reply);
            seL4_Reply (reply);

            pawpaw_cspace_free_slot (resp_cap);
            #endif

        } else if (label == seL4_Interrupt) {
            printf ("*** NEW DEVICE ADDED\n");
            /* ask to talk to it */

            seL4_CPtr dev_cap = pawpaw_cspace_alloc_slot ();
            assert (dev_cap);
            seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, dev_cap, PAPAYA_CSPACE_DEPTH);

            seL4_Word dev_id = seL4_GetMR (0);

            msg = seL4_MessageInfo_new (0, 0, 1, 2);
            seL4_SetMR (0, DEV_GET_INFO);
            seL4_SetCap (0, dev_cap);
            seL4_SetMR (1, dev_id);

            seL4_Call (dev_ep, msg);

            /* put back receive path */
            seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, resp_cap, PAPAYA_CSPACE_DEPTH);

            /* allocate it */
            struct ventry* ve = malloc (sizeof (struct ventry));
            seL4_Word type = seL4_GetMR (0);

            if (type == 0) {
                ve->name = "console";
            } else if (type == 1) {
                ve->name = "timer";
            } else {
                /* FIXME: test I don't think this works but it's 3AM and probably will never run */
                ve->name = "unkdev";
                //ve->name = strcat (ve->name, itoa ((int)seL4_GetMR (1)));
            }

            ve->vnode = dev_cap;
            ve->next = NULL;

            if (entries) {
                ve->next = entries;
            }

            entries = ve;

            printf ("registered inside fs dev\n");
        }
    }
}
