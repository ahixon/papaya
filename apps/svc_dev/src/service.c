#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>

#include <sos.h>

enum device_type {
    DEV_CONSOLE,
    DEV_TIMER,
    DEV_ETHERNET,
    DEV_AUDIO,
    DEV_VIDEO,
};

enum device_loc {
    PLATFORM_DEVICE,
    USB,
    SATA,
};

#define DEV_LISTEN_CHANGES      20
#define DEV_REGISTER            21
#define DEV_GET_INFO            25

/* crappy linked-list of registered devices
 * much TODO here:
 *  - THIS SHOULD REALLY BE A TREE STRUCTURE
 *      - hence current fields kinda suck/will change
 *
 *  - devices should be asking svc_dev about mapping devices and so on, not root server
 */
struct device {
    int list_revision;          /* revision device was added to list */

    unsigned int id;            /* device ID connected in this system - should be unique???? */

    char* name;
    enum device_type type;      /* CONSOLE, TIMER, ETHERNET, PHY, VIDEO, AUDIO, BLOCK, CHAR etc */
    enum device_loc bus;        /* PLATFORM_DEVICE, PCI, USB */

    unsigned int vid;   /* vendor ID */
    unsigned int pid;   /* product ID */
    unsigned int sid;   /* subproduct ID */

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

int main(void) {
    int err;
    seL4_Word badge;
    seL4_MessageInfo_t message, reply;

    /* create our EP to listen on */
    printf ("svc_dev: creating EP\n");
    seL4_CPtr service_cap = pawpaw_create_ep ();
    assert (service_cap);

    printf ("svc_dev: allocating msg slot\n");
    seL4_CPtr msg_cap = pawpaw_cspace_alloc_slot ();
    assert (msg_cap);

    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, msg_cap, PAPAYA_CSPACE_DEPTH);

    printf ("svc_dev: registering service\n");
    pawpaw_register_service (service_cap);
    printf ("svc_dev: started\n");

    while (1) {
        /* wait for a message */
        message = seL4_Wait(service_cap, &badge);
        seL4_CPtr reply_cap = pawpaw_save_reply ();
        uint32_t label = seL4_MessageInfo_get_label(message);

        printf ("** SVC_DEV ** received message from %x with label %d and length %d\n", badge, label, seL4_MessageInfo_get_length (message));

        if (label == seL4_NoError) {
            if (seL4_GetMR (0) == DEV_LISTEN_CHANGES) {
                if (seL4_MessageInfo_get_extraCaps (message) != 1) {
                    printf ("got no cap to send stuff to\n");
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

                seL4_CPtr msg_cap = pawpaw_cspace_alloc_slot ();
                assert (msg_cap);
                seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, msg_cap, PAPAYA_CSPACE_DEPTH);

                printf ("some thread is now listening for changes\n");
                seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);
                seL4_SetMR (0, 0);

                seL4_Send (reply_cap, reply);

            } else if (seL4_GetMR (0) == DEV_REGISTER) {
                if (seL4_MessageInfo_get_extraCaps (message) != 1) {
                    printf ("register: didn't get cap to device\n");
                    continue;
                }

                /*char* s = pawpaw_bean_get (mycan, seL4_GetMR (4));
                printf ("passed in: %s\n", s);*/

                // FIXME: man this is crap
                //s[256] = '\0';

                struct device* dev = malloc (sizeof (struct device));
                dev->next = NULL;
                //dev->name = strdup (s);
                dev->name = NULL;
                dev->type = seL4_GetMR (1);
                dev->bus = seL4_GetMR (2);
                dev->pid = seL4_GetMR (3);

                /* FIXME: once we come up with a better way - perhaps struct across bean */
                dev->vid = 0;
                dev->sid = 0;

                dev->msg_cap = msg_cap;
                dev->id = device_generate_id (dev);

                if (devlist) {
                    dev->next = devlist;
                }

                devlist = dev;

                seL4_CPtr msg_cap = pawpaw_cspace_alloc_slot ();
                assert (msg_cap);
                seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, msg_cap, PAPAYA_CSPACE_DEPTH);

                notify_registered (dev);
            } else if (seL4_GetMR (0) == DEV_GET_INFO) {
                unsigned int id = seL4_GetMR (1);

                struct device* dev = devlist;
                while (dev) {
                    if (dev->id == id) {
                        break;
                    }

                    dev = dev->next;
                }

                if (dev) {
                    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 1, 2);
                    seL4_SetMR (0, dev->type);
                    //seL4_SetMR (1, )

                    // FIXME: do I need to badge this?
                    seL4_SetCap (0, dev->msg_cap);

                    seL4_Send (reply_cap, reply);
                } else {
                    printf ("UNKNOWN DEVICE ID 0x%x\n", dev);
                }

            }
        }
    }
}
