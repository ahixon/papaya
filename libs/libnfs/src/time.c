#include "time.h"
#include "pbuf_helpers.h"
#include "common.h"

#include <sel4/sel4.h>
#include <pawpaw.h>
#include <network.h>
#include <lwip/udp.h>
#include <assert.h>


#define DEBUG_TIME 1
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
static volatile uint32_t utc1900_seconds = 0;

/*
static void
time_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
          struct ip_addr *addr, u16_t port)
{
    int pos = 0;
    pb_readl(p, (uint32_t*)&utc1900_seconds, &pos);
    debug("received time %u\n", utc1900_seconds);
    pbuf_free(p);
}
*/

uint32_t 
udp_time_get(const struct ip_addr *server)
{
    seL4_MessageInfo_t msg;

    //struct pbuf *pbuf;
    //int retry_count;
    //struct ip_addr s = *server;

    seL4_CPtr wait_ep = pawpaw_create_ep ();
    assert (wait_ep);

    /* Create a connection to the time server */
    msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, wait_ep);        /* FIXME: make this async EP instead to get rid of hack */
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER);
    seL4_SetMR (1, NETSVC_PROTOCOL_UDP);
    seL4_SetMR (2, TIME_PORT);
    seL4_SetMR (3, (seL4_Word)s[0]);  /* phew, this fits into u32 */

    seL4_Call (net_ep, msg);
    if (seL4_GetMR (0) != 0) {
        printf ("time: net_svc register failed\n");
        return 0;
    }

    /* now send empty packet to server to get time */
    struct pawpaw_share* share = pawpaw_share_new ();
    assert (share);

    msg = seL4_MessageInfo_new (0, 0, 1, 3);
    seL4_SetCap (0, share->cap);
    seL4_SetMR (0, NETSVC_SERVICE_SEND);
    seL4_SetMR (1, share->id);
    seL4_SetMR (2, 0);
    seL4_Call (net_ep, msg);

    seL4_Wait (wait_ep, NULL);
    printf ("got reply I think\n");

    msg = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, NETSVC_SERVICE_DATA);
    seL4_Call (net_ep, msg);

    seL4_Word size = seL4_GetMR (1);
    printf ("reply size was 0x%x\n", size);
    return 0;
#if 0
    utc1900_seconds = 0;
    retry_count = TIME_RETRIES;
    while(utc1900_seconds == 0 && retry_count-- >= 0){
        int cnt_out;
        int err;

        /* 
         * Sending an empty packet registers ourselves with the server
         * LWIP does not preserve the pbuf so we need a new one each time.
         */
        pbuf = pbuf_alloc(PBUF_TRANSPORT, TIME_PAYLOAD_SIZE, PBUF_RAM);
        assert(pbuf);
        err = udp_send(time_pcb, pbuf);
        if(err){
            debug("time: udp transmission err: %d\n", err);
            break;
        }
        pbuf_free(pbuf);

        /* Wait for a reply */
        cnt_out = 0;
        while(utc1900_seconds == 0 && cnt_out < TIME_RETRY_TO_US){
            _usleep(TIME_DELAY_US);
            cnt_out += TIME_DELAY_US;
        }
    }

    /* Clean up and exit */
    udp_remove(time_pcb);
    return utc1900_seconds;
#endif
}

