#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <sos.h>

int main(void) {
    printf ("Network service started.");

    while (1) {
        seL4_Word badge;
        seL4_Word interrupts_fired;
        seL4_MessageInfo_t message;

        message = seL4_Wait((SYSCALL_SERVICE_SLOT + 1), &badge);

        uint32_t label = seL4_MessageInfo_get_label(message);
        printf ("** SVC_NET ** received message from %x with label %d and length %d\n", badge, label, seL4_MessageInfo_get_length (message));
    }
}
