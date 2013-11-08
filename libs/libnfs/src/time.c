#include "time.h"
#include "pbuf_helpers.h"
#include "common.h"

#include <sel4/sel4.h>
#include <pawpaw.h>
#include <network.h>
#include <lwip/udp.h>
#include <assert.h>


//#define DEBUG_TIME 1
#ifdef DEBUG_TIME
#define debug(x...) printf( x )
#else
#define debug(x...)
#endif


#define TIME_PORT            37

#define TIME_RETRIES          5
#define TIME_RETRY_TO_US  10000
#define TIME_DELAY_US      1000

#define TIME_PAYLOAD_SIZE     0 /* We don't need a payload, just a header */

extern seL4_CPtr net_ep;

uint32_t 
udp_time_get(const struct ip_addr *server)
{
    seL4_MessageInfo_t msg;

    seL4_CPtr wait_ep = pawpaw_create_ep_async ();
    assert (wait_ep);

    /* Create a connection to the time server */
    msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, wait_ep);        /* FIXME: make this async EP instead to get rid of hack */
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER);
    seL4_SetMR (1, NETSVC_PROTOCOL_UDP);
    seL4_SetMR (2, TIME_PORT);
    seL4_SetMR (3, 0);
    int net_id = seL4_GetMR (0);
    assert (net_id >= 0);
    // FIXME: implement this
    //seL4_SetMR (3, (seL4_Word)s[0]);  /* phew, this fits into u32 */

    seL4_Call (net_ep, msg);
    if (seL4_GetMR (0) != 0) {
        printf ("time: net_svc register failed\n");
        return 0;
    }

    debug ("time: got result, making share\n");
    
    /* now send empty packet to server to get time */
    struct pawpaw_share* share = pawpaw_share_new ();
    assert (share);

    debug ("time: got share, sending data for service 0x%x\n", net_id);

    msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, share->cap);
    seL4_SetMR (0, NETSVC_SERVICE_SEND);
    seL4_SetMR (1, share->id);
    seL4_SetMR (2, net_id);
    seL4_SetMR (3, 0);
    seL4_Call (net_ep, msg);

    debug ("time: sent ok, WAITING ON EP for result\n");

    /* FIXME: need a timeout - or not since it's a separate server */
    seL4_Wait (wait_ep, NULL);

    debug ("time: got result, asking netsvc for data\n");
    msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, NETSVC_SERVICE_DATA);
    seL4_SetMR (1, net_id);
    seL4_Call (net_ep, msg);


    seL4_Word size = seL4_GetMR (1);
    debug ("time: got 0x%x bytes\n", size);
    assert (size == 4); /* FIXME: is this OK to test for? */

    debug ("time: mounting share for result\n");
    struct pawpaw_share* result_share = pawpaw_share_mount (pawpaw_event_get_recv_cap ());
    assert (result_share);

    char* cbuf = result_share->buf;
    unsigned int utc1900_seconds = ntohl (*(u32_t*)(cbuf));  /* mmm, yum */
    debug ("time: got time %u (since 1/1/1900)\n", utc1900_seconds);

    pawpaw_share_unmount (result_share);
    /* FIXME: should cleanup UDP client */

    return utc1900_seconds;
}

