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

seL4_CPtr service_ep = 0;

int vfs_open (struct pawpaw_event* evt);
int vfs_read (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   0,  0,  0   },      //              //
    {   0,  0,  0   },      //   RESERVED   //
    {   0,  0,  0   },      //              //
    {   vfs_open,           2,  HANDLER_REPLY },    // mode + badge, replies with EP to file (badged version of listen cap)
    {   vfs_read,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT },    // num bytes
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers };


int have_reader = false;

int vfs_open (struct pawpaw_event* evt) {
    if (evt->args[0] & FM_READ) {
        if (have_reader) {
            printf ("console: already opened for reading\n");
            return PAWPAW_EVENT_UNHANDLED;
        }

        have_reader = true;
    }

    /* FIXME: register in open FD table so that opened for writing can't read and vice versa */

    printf ("console: cool OK opened, giving back our cap\n");
    seL4_SetCap (0, service_ep);
    seL4_SetMR (0, 0);
    evt->reply = seL4_MessageInfo_new (0, 0, 1, 1);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_read (struct pawpaw_event* evt) {
    printf ("*** WANT TO READ %d bytes\n", evt->args[0]);
    return PAWPAW_EVENT_UNHANDLED;
}

int main (void) {
    /* ask the root server for network driver deets */
    /* we should get back a cap that we can communicate directly with it */
    /*seL4_CPtr net_ep = pawpaw_service_lookup ("sys.net.services");
    assert (net_ep);*/

    pawpaw_event_init ();

    service_ep = pawpaw_create_ep ();
    assert (service_ep);

    seL4_CPtr dev_ep = pawpaw_service_lookup ("svc_dev");

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetMR (0, DEV_REGISTER);
    
    seL4_SetCap (0, service_ep);

    seL4_SetMR (1, 0);              // type = console
    seL4_SetMR (2, 0);              // bus = platform device
    seL4_SetMR (3, 1337);           // product ID = 1337 lol??

    /* FIXME: do we REALLY need Call? at the moment, no lol */
    printf ("dev_console: registering device\n");
    seL4_Send (dev_ep, msg);

#if 0
    
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER | NETSVC_PROTOCOL_UDP);
    seL4_SetMR (0, 20);
    seL4_SetMR (1, 26706);    

    /* ask it to register us on UDP port 26706 with OUR provided mbox frame */
    /* what about mbox circular buffers - would be nice otherwise we have to copy out into our buffer
        - depends how common a use case this is - if it is common, then implement it in the API layer so we get zerocopy */

    msg = seL4_MessageInfo_new (0, 0, /*XXX: 1*/0, 2);
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER | NETSVC_PROTOCOL_UDP);
    seL4_SetMR (0, 20);
    seL4_SetMR (1, 26706);

    /* copy our mbox cap so we can give it to the netsvc to load in our data */
    /* FIXME: can we copy directly into destination thread's addrspace? probably not? */
    //seL4_CPtr server_mbox = cspace_copy_cap (cur_cspace, cur_cspace, MBOX_CAPS[0], seL4_AllRights); 
    // fixme: also do we want all rights?

    // does this go in msg?
    //seL4_SetCap (0, server_mbox);
    printf ("About to call on net_ep %d\n", net_ep);
    msg = seL4_Call(net_ep, msg);

    printf ("GOT RESPONSE, error = %s and reg 0 = %d\n", seL4_Error_Message(seL4_MessageInfo_get_label (msg)), seL4_GetMR(0));

    // make sure no errors
    assert (seL4_GetMR (0) == 0);

    printf ("You are awesome.\n");
#endif

    pawpaw_event_loop (&handler_table, NULL, service_ep);

    /*if (my_ep is interrupt) {
        // check badge, probably from NETSVC
    } else if (my_ep is message) {
        // probably read/write message
        // read from buffer and respond straight away, or add to internal queue and wait (wake thread up on interrupt)
        // could optimise: if buffer is empty, and wants to read N bytes, could ask netsvc to load into their mbox directly? zerocopy?
        // problem here is you'd need to setup netsvc to READ ONLY N BYTES AND UNREGISTER
    }*/

    /* now we play the waiting game... */
    /* read/write will contain:
            - cap to src/destination page/mbox
            - reply cap to notify when done
            - size (<= page size)
            - offset (to help with zero-copy buffers)

        can respond with:
            - bytes written (success/partial success)
            - cap invalid
            - read/write failed (device specific error?)

        if we get any reads add it to our buffer
        (until we're full, at which point start throwing away stuff)
        ^ goes into our mbox, might need to move out into another buffer (see above about integrating circular buf into API)
    */

    return 0;
}
