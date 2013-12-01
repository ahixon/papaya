#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>

#include <sos.h>
#include <device.h>

/* boring linked-list of registered devices
 * TODO: this should really be a tree structure instead
 */
struct device {
    int list_revision;              /* revision device was added to list */

    unsigned int id;                /* device ID connected in this system -
                                     * should be unique */

    char* name;
    enum svcdev_device_type type;   /* CONSOLE, TIMER, ETHERNET, BLOCK etc */
    enum svcdev_device_loc bus;     /* PLATFORM_DEVICE, PCI, USB */

    unsigned int vid;               /* vendor ID */
    unsigned int pid;               /* product ID */
    unsigned int sid;               /* subproduct ID */

    seL4_CPtr msg_cap;

    struct device* next;
};

/* people listening for events */
struct client {
    seL4_CPtr their_cap;
    unsigned int interested_in;

    struct client* next;
};

struct device* devlist;
struct client* clients;

void notify_registered (struct device* d) {
    struct client* c = clients;
    while (c) {
        seL4_Notify (c->their_cap, d->id);
        c = c->next;
    }
}

unsigned int device_generate_id (struct device* d) {
    // TODO: make this ACTUALLY unique
    return (7 * d->pid) + (11 * d->vid) + (13 * d->sid);
}

int main (void) {
    seL4_Word badge;
    seL4_MessageInfo_t message;

    /* create our EP to listen on */
    seL4_CPtr service_cap = pawpaw_create_ep ();
    if (!service_cap) {
        return -1;
    }

    seL4_CPtr msg_cap = pawpaw_cspace_alloc_slot ();
    if (!msg_cap) {
        return -1;
    }

    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, msg_cap,
        PAPAYA_CSPACE_DEPTH);

    pawpaw_register_service (service_cap);

    while (1) {
        /* wait for a message */
        message = seL4_Wait(service_cap, &badge);
        seL4_CPtr reply_cap = pawpaw_save_reply ();
        uint32_t label = seL4_MessageInfo_get_label(message);


        if (label == seL4_NoError) {
            if (seL4_GetMR (0) == DEVSVC_LISTEN_CHANGES) {
                if (seL4_MessageInfo_get_extraCaps (message) != 1) {
                    continue;
                }

                struct client* c = malloc (sizeof (struct client));
                c->their_cap = msg_cap;
                c->next = NULL;
                c->interested_in = seL4_GetMR (1);

                if (clients) {
                    c->next = clients;
                }

                clients = c;

                msg_cap = pawpaw_cspace_alloc_slot ();
                if (!msg_cap) {
                    free (c);
                    break;
                }

                seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, msg_cap,
                    PAPAYA_CSPACE_DEPTH);

                seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);
                seL4_SetMR (0, 0);

                seL4_Send (reply_cap, reply);

                /* notify for all existing devices */
                struct device* dev = devlist;
                while (dev) {
                    seL4_Notify (c->their_cap, dev->id);
                    dev = dev->next;
                }


            } else if (seL4_GetMR (0) == DEVSVC_REGISTER) {
                /* ensure cap to device is not missing */
                if (seL4_MessageInfo_get_extraCaps (message) != 1) {
                    continue;
                }

                struct device* dev = malloc (sizeof (struct device));
                dev->next = NULL;
                dev->name = NULL;
                dev->type = seL4_GetMR (1);
                dev->bus = seL4_GetMR (2);
                dev->pid = seL4_GetMR (3);

                dev->vid = 0;
                dev->sid = 0;

                dev->msg_cap = msg_cap;
                dev->id = device_generate_id (dev);

                if (devlist) {
                    dev->next = devlist;
                }

                devlist = dev;

                msg_cap = pawpaw_cspace_alloc_slot ();
                if (!msg_cap) {
                    free (dev);
                    break;
                }
                
                seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, msg_cap,
                    PAPAYA_CSPACE_DEPTH);

                notify_registered (dev);
            } else if (seL4_GetMR (0) == DEVSVC_GET_INFO) {
                unsigned int id = seL4_GetMR (1);

                struct device* dev = devlist;
                while (dev) {
                    if (dev->id == id) {
                        break;
                    }

                    dev = dev->next;
                }

                if (dev) {
                    seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 1, 2);
                    seL4_SetMR (0, dev->type);
                    seL4_SetCap (0, dev->msg_cap);

                    seL4_Send (reply_cap, reply);
                }
            }
        }
    }
}
