#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>
#include <network.h>

#include <sos.h>

seL4_CPtr net_ep = 0;

void interrupt_handler (struct pawpaw_event* evt) {
    /* collect the data */
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, NETSVC_SERVICE_DATA);
    seL4_Call (net_ep, msg);

    seL4_Word size = seL4_GetMR (1);

    /* mount it if we didn't have it already */
    struct pawpaw_share* netshare = pawpaw_share_get (seL4_GetMR (0));
    if (!netshare) {
        netshare = pawpaw_share_mount (pawpaw_event_get_recv_cap ());
        assert (netshare);

        pawpaw_share_set (netshare);
    }

    /* print it out */
    assert (size < PAPAYA_IPC_PAGE_SIZE);
    ((char*)netshare->buf)[size] = '\0';
    printf ("echoserver: got '%s', length 0x%x\n", (char*)netshare->buf, size);

    /* and send it right back */
    msg = seL4_MessageInfo_new (0, 0, 1, 3);
    seL4_SetCap (0, netshare->cap);
    
    seL4_SetMR (0, NETSVC_SERVICE_SEND);
    seL4_SetMR (1, netshare->id);
    seL4_SetMR (2, size);
    seL4_Call (net_ep, msg);    /* FIXME: should handle Call or Send */

    //pawpaw_share_unmount (netshare);
}

int main (void) {
    pawpaw_event_init ();

    /* async ep to receive network data notifications */
    seL4_CPtr async_ep = pawpaw_create_ep_async ();
    assert (async_ep);

    /*err = seL4_TCB_BindAEP (PAPAYA_TCB_SLOT, async_ep);
    assert (!err);*/

    net_ep = pawpaw_service_lookup ("svc_net");
    assert (net_ep);

    sleep (200);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 3);
    seL4_SetCap (0, async_ep);
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER);
    seL4_SetMR (1, NETSVC_PROTOCOL_UDP);
    seL4_SetMR (2, 26706);

    seL4_Call (net_ep, msg);
    assert (seL4_GetMR (0) == 0);

    sleep (200);

    struct pawpaw_share* helloshare = pawpaw_share_new ();
    memcpy (helloshare->buf, "hello!", strlen("hello!"));
    seL4_SetCap (0, helloshare->cap);
    seL4_SetMR (0, NETSVC_SERVICE_SEND);
    seL4_SetMR (1, helloshare->id);
    seL4_SetMR (2, strlen("hello!"));

    seL4_Call (net_ep, msg);
    assert (seL4_GetMR (0) == 0);

    /*pawpaw_event_loop (&handler_table, interrupt_handler, async_ep);*/
    printf ("echoserver: ready, waiting for interrupt...\n");
    while (1) {
        seL4_Word badge = 0;
        seL4_MessageInfo_t msg = seL4_Wait (async_ep, &badge);

        seL4_Word label = seL4_MessageInfo_get_label (msg);
        if (label != seL4_Interrupt) {
            printf ("echoserver: received message that wasn't interrupt\n");
            continue;
        }

        struct pawpaw_event* evt = pawpaw_event_create (msg, badge);
        if (!evt) {
            /* silently drop event */
            pawpaw_event_dispose (evt);
            continue;
        }

        evt->args = malloc (sizeof (seL4_Word));
        assert (evt->args);
        evt->args[0] = seL4_GetMR (0);

        interrupt_handler (evt);
        pawpaw_event_dispose (evt);
    }

    return 0;
}
