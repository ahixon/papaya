#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sos.h>

int main(void) {
    /* first register with NFS because i need those sweet, sweet ca(ns|ps) Mr Simpson nooOOoOOoOOOOooo!!
    Dramatisation: may not have happened. */

    /* ask the root server for network driver deets */
    /* we should get back a cap that we can communicate directly with it */
    char* service = "net.services";
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 3);
    seL4_SetTag (msg);  /* set IPC? */
    seL4_SetMR(0, SYSCALL_FIND_SERVICE);
    seL4_SetMR(1, &service);

    /* prep IPC buffer for cap transfer */
    ipc_buf = &IPC_BUFFER_VSTART;
    // cspace_find_freeslot()?
    seL4_CPtr net_ep = cspace_alloc_slot (cur_cspace);
    ipc_buf->receiveCNode = net_ep;
    /*
    ipc_buf->receiveIndex = 0;  // relateive to receiveCNode
    ipc_buf->receiveDepth = 0;  // bits of Index to use
    */


    /* NOTE: SYSCALL_ENDPOINT_SLOT needs to have Grant */
    seL4_Call (SYSCALL_ENDPOINT_SLOT, msg);

    // check for errors better (ie svcman might return SERVICE_NOT_FOUND, SERVICE_DENIED, etc).
    assert (seL4_GetMR (0) == 0);

    /* ask it to register us on UDP port 26706 with OUR provided mbox frame */
    /* what about mbox circular buffers - would be nice otherwise we have to copy out into our buffer
        - depends how common a use case this is - if it is common, then implement it in the API layer so we get zerocopy */
    msg = seL4_MessageInfo_new (0, 1, 0, 2);
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER | NETSVC_PROTOCOL_UDP);
    seL4_SetMR (1, 26706);

    /* copy our mbox cap so we can give it to the netsvc to load in our data */
    /* FIXME: can we copy directly into destination thread's addrspace? probably not? */
    seL4_CPtr server_mbox = cspace_copy_cap (cur_cspace, cur_cspace, MBOX_CAPS[0], seL4_AllRights); 
    // fixme: also do we want all rights?

    // does this go in msg?
    ipc_buf->caps[0] = server_mbox;
    seL4_Call(net_ep, msg);

    while (seL4_Wait (my_ep)) {
        if (my_ep is interrupt) {
            // check badge, probably from NETSVC
        } else if (my_ep is message) {
            // probably read/write message
            // read from buffer and respond straight away, or add to internal queue and wait (wake thread up on interrupt)
            // could optimise: if buffer is empty, and wants to read N bytes, could ask netsvc to load into their mbox directly? zerocopy?
            // problem here is you'd need to setup netsvc to READ ONLY N BYTES AND UNREGISTER
        }
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

    while (seL4_Wait (&msg)) {
        if (msg == READ) {

        }
    }
}
