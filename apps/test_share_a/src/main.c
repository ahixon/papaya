#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>

#include <sos.h>

#define MSG_PREFIX ("Hello from A #%d")
#define NUM_RUNS    32

int main(void) {
    char msg[32] = {0};

    printf ("\t\tA: creating service EP\n");
    seL4_CPtr service_ep = pawpaw_create_ep ();

    printf ("\t\tA: registering ourselves\n");
    pawpaw_register_service (service_ep);

    printf ("\t\tA: creating shared buffer...\n");
    struct pawpaw_share *share = pawpaw_share_new ();
    assert (share);

    printf ("\t\tA: created with id %d\n", share->id);

    printf ("\t\tA: starting test_share_b\n");
    assert (process_create ("test_share_b") >= 0);

    printf ("\t\tA: looking up its service\n");
    seL4_CPtr b = pawpaw_service_lookup ("test_share_b");
    assert (b);

    for (int i = 0; i < NUM_RUNS; i++) {
        /*strcpy (msg, MSG_PREFIX);
        strcat (msg, itoa (i));*/
        sprintf (msg, MSG_PREFIX, i);

        printf ("\t\tA: copying to buffer @ %p\n", share->buf);
        memcpy (share->buf, msg, strlen (msg));

        printf ("\t\tA: calling B...\n");
        seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, !share->sent, 1);
        //pawpaw_share_attach (share);
        if (!share->sent) {
            printf ("\t\tA: attaching cap\n");
            seL4_SetCap (0, share->cap);
            share->sent = true;
        };
        
        seL4_SetMR (0, share->id);
        seL4_Call (b, msg);

        printf ("\t\tA: received %s\n", share->buf);
    }

    printf ("\t\tdone - bye!\n");

    return 0;
}