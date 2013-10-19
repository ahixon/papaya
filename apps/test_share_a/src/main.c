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
//#define NUM_RUNS    0x1000000
#define NUM_RUNS    8

int main(void) {
    char msg[32] = {0};

    printf ("\t\tA: creating service EP\n");
    seL4_CPtr service_ep = pawpaw_create_ep ();

    printf ("\t\tA: allocating slot\n");
    seL4_CPtr share_cap = pawpaw_cspace_alloc_slot ();
    assert (share_cap);

    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, share_cap, PAPAYA_CSPACE_DEPTH);

    printf ("\t\tA: registering ourselves\n");
    pawpaw_register_service (service_ep);

    printf ("\t\tA: creating shared buffer...\n");
    struct pawpaw_share *share = pawpaw_share_new ();
    assert (share);

    printf ("\t\tA: created with id %d\n", share->id);

    printf ("\t\tA: starting test_share_b\n");
    pid_t b_pid = process_create ("test_share_b");
    assert (b_pid >= 0);

    printf ("\t\tA: looking up its service\n");
    seL4_CPtr b = pawpaw_service_lookup ("test_share_b");
    assert (b);

    for (int i = 0; i < NUM_RUNS; i++) {
        sprintf (msg, MSG_PREFIX, i);

        printf ("\t\tA: copying to buffer @ %p\n", share->buf);
        memcpy (share->buf, msg, strlen (msg));

        //printf ("\t\tA: calling B...\n");
        seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, pawpaw_share_attach (share), 1);
        
        seL4_SetMR (0, share->id);
        seL4_MessageInfo_t reply = seL4_Call (b, msg);

        printf ("\t\tA: received in its buffer %s\n", share->buf);

        if (seL4_MessageInfo_get_extraCaps (reply) == 1) {
            seL4_Word msgid = seL4_GetMR (0);
            printf ("\t\tA: recevied cap from B, mounting\n");
            printf ("\t\tA: msg reported share ID 0x%x\n", msgid);
            struct pawpaw_share *tmpshare = pawpaw_share_mount (share_cap);
            assert (tmpshare);
            printf ("\t\tA: mounted reported ID 0x%x\n", tmpshare->id);

            assert (tmpshare->id == msgid);

            printf ("\t\tA: content mounted to vaddr %p was: %s\n", tmpshare->buf, tmpshare->buf);

            /*share_cap = pawpaw_cspace_alloc_slot ();
            assert (share_cap);*/

            assert (pawpaw_share_unmount (tmpshare));
            //pawpaw_cspace_free_slot (share_cap);
            seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, share_cap, PAPAYA_CSPACE_DEPTH);
        }
    }

    printf ("\t\tfinished, killing B\n");
    process_delete (b_pid);
    printf ("\t\tdone - bye!\n");

    return 0;
}
