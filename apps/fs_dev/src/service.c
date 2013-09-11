#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <sos.h>

int main(void) {
    char* service = "sys.dev";
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 3);

    /* FIXME: yuck */
    seL4_CPtr net_ep = SYSCALL_SERVICE_SLOT;
    seL4_SetCapReceivePath (4, net_ep, CSPACE_DEPTH);

    seL4_SetMR(0, SYSCALL_FIND_SERVICE);
    seL4_SetMR(1, (seL4_Word)service);
    seL4_SetMR(2, strlen (service));

    seL4_MessageInfo_t reply = seL4_Call (SYSCALL_ENDPOINT_SLOT, msg);

    // check for errors better (ie svcman might return SERVICE_NOT_FOUND, SERVICE_DENIED, etc).
    assert (seL4_GetMR (0) == 0);

    /* OK ask the device service to notify us when a device is added/removed */
    msg = seL4_MessageInfo_new(0, 0, 1, 1);
    seL4_SetMR (0, DEV_LISTEN_CHANGES);

    while (1) {
        seL4_Word badge;
        seL4_Word interrupts_fired;
        seL4_MessageInfo_t message;

        message = seL4_Wait(SYSCALL_LISTEN_SLOT, &badge);

        uint32_t label = seL4_MessageInfo_get_label(message);
        printf ("** SVC_NET ** received message from %x with label %d and length %d\n", badge, label, seL4_MessageInfo_get_length (message));

        seL4_MessageInfo_t newmsg = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);
        seL4_Reply (newmsg);
    }
}
