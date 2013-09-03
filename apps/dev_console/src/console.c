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
    seL4_Call();

    /* ask it to register us on UDP port 26706 with OUR provided mbox frame */
    /* what about mbox circular buffers - would be nice otherwise we have to copy out into our buffer
        - depends how common a use case this is - if it is common, then implement it in the API layer so we get zerocopy */

    /* now register with root server, since we're ready */
    seL4_Call();

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
