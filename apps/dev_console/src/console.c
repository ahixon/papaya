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

#define DEV_REGISTER            21

int main(void) {
    
    for (int i = 0; i < 350; i++) {
        seL4_Yield();
    }
    //printf ("###############################\nsosh: continuing...\n");
    /*
     * I need those sweet, sweet caps.. Mr Simpson nooOOoOOoOOOOooo!!
     * Dramatisation: may not have happened.
     */

    /* ask the root server for network driver deets */
    /* we should get back a cap that we can communicate directly with it */
    /*seL4_CPtr net_ep = pawpaw_service_lookup ("sys.net.services");
    assert (net_ep);*/

    seL4_CPtr service_ep = pawpaw_create_ep ();
    assert (service_ep);

    seL4_CPtr dev_ep = 0;
    while (!dev_ep) {
        dev_ep = pawpaw_service_lookup ("sys.dev");
        if (!dev_ep) {
            seL4_Yield();
        }
    }

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetMR (0, DEV_REGISTER);
    
    seL4_SetCap (0, service_ep);

    seL4_SetMR (1, 0);              // type = console
    seL4_SetMR (2, 0);              // bus = platform device
    seL4_SetMR (3, 1337);           // product ID = 1337 lol??

    /* FIXME: do we REALLY need Call? */
    printf ("dev_console: registering device\n");
    seL4_Call (dev_ep, msg);

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

    seL4_Word sender_badge;
    //seL4_MessageInfo msg;
    while (1) {
        msg = seL4_Wait (service_ep, &sender_badge);

        /*if (my_ep is interrupt) {
            // check badge, probably from NETSVC
        } else if (my_ep is message) {
            // probably read/write message
            // read from buffer and respond straight away, or add to internal queue and wait (wake thread up on interrupt)
            // could optimise: if buffer is empty, and wants to read N bytes, could ask netsvc to load into their mbox directly? zerocopy?
            // problem here is you'd need to setup netsvc to READ ONLY N BYTES AND UNREGISTER
        }*/
        printf ("@@@@@@@@@@@@@@@@@@@@@@\ndev_console: Got message from somebody!\n@@@@@@@@@@@@@@@@@@@@@@@@@\n");
    }

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


        need helper library for mbox selection?
        (bit field to determine which is free/not?)

        if we get any reads add it to our buffer
        (until we're full, at which point start throwing away stuff)
        ^ goes into our mbox, might need to move out into another buffer (see above about integrating circular buf into API)
    */

    return 0;
}
