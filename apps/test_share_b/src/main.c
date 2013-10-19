#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>

#include <sos.h>

#define MSG_CHANGED ("hello lol\0")
#define MSG_PREFIX ("Hello from B #%d")

int main(void) {
    char msg[32] = {0};

    printf ("\t\tB: creating service EP\n");
    seL4_CPtr service_ep = pawpaw_create_ep ();

    printf ("\t\tB: registering ourselves\n");
    pawpaw_register_service (service_ep);

    int i = 0;

    printf ("\t\tB: allocating slot\n");
    seL4_CPtr share_cap = pawpaw_cspace_alloc_slot ();
    assert (share_cap);

    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, share_cap, PAPAYA_CSPACE_DEPTH);

    char* buf = NULL;

    while (1) {
        seL4_MessageInfo_t ipc_msg = seL4_Wait (service_ep, NULL);

        if (seL4_MessageInfo_get_extraCaps (ipc_msg) == 1) {
            struct pawpaw_share *share = pawpaw_share_mount (share_cap);
            assert (share);

            printf ("\t\tB: mounting share from A with ID 0x%x (msg ID was 0x%x)\n", share->id, seL4_GetMR (0));
            printf ("\t\tB: buffer pointer was %p\n", share->buf);

            buf = share->buf;
        }

        assert (buf);
        printf ("\t\tB: received %s\n", buf);

        sprintf (msg, MSG_PREFIX, i);
        memcpy (buf, MSG_CHANGED, strlen(MSG_CHANGED) + 1);
        i++;

        struct pawpaw_share *newshare = pawpaw_share_new ();
        assert (newshare);

        printf ("\t\tB: created new share, ID 0x%x\n", newshare->id);
        printf ("\t\tB: copying our message to %p\n", newshare->buf);

        memcpy (newshare->buf, msg, strlen (msg));
        seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 1, 1);
        pawpaw_share_attach (newshare);

        seL4_SetMR (0, newshare->id);

        seL4_Reply (reply);
    }

    return 0;


#if 0
    printf ("\t\tcreating shared buffer...\n");
    share_t share = pawpaw_share_new ();
    assert (share);

    printf ("\t\tcreated with id %d\n", share->id);

    printf ("\t\tstarting test_share_b\n");
    assert (process_create ("test_share_b") >= 0);

    printf ("\t\tlooking up its service\n");
    seL4_CPtr b = pawpaw_service_lookup ("test_share_b");
    assert (b);

    for (int i = 0; i < NUM_RUNS; i++) {
        strcpy (msg, MSG_PREFIX);
        strcat (msg, itoa (i));

        printf ("\t\tcopying to buffer @ %p\n", share->buf);
        memcpy (share->buf, msg);

        printf ("\t\tcalling B...\n");
        seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 1);
        //pawpaw_share_attach (share);
        if (!share->sent) {
            seL4_SetCap (0, share->cap);
            share->sent = true
        };
        
        seL4_SetMR (0, share->id);
        seL4_Call (b, msg);

        printf ("\t\tcontents of buffer: %s\n", share->buf);
    }

    printf ("\t\tdone - bye!\n");
#endif
}