#include "time.h"
#include "pbuf_helpers.h"
#include "common.h"

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


static volatile uint32_t utc1900_seconds = 0;

static void
time_recv(void *arg, struct udp_pcb *upcb, struct pbuf *p,
          struct ip_addr *addr, u16_t port)
{
    int pos = 0;
    pb_readl(p, (uint32_t*)&utc1900_seconds, &pos);
    debug("received time %u\n", utc1900_seconds);
    pbuf_free(p);
}

uint32_t 
udp_time_get(const struct ip_addr *server)
{
    struct udp_pcb *time_pcb = NULL;
    struct pbuf *pbuf;
    int retry_count;
    struct ip_addr s = *server;
    /* Create a connection to the time server */
    time_pcb = udp_new();
    if(time_pcb == NULL){
        printf("time: failed to create udp pcb\n");
        return 0;
    }

    debug("time: setup udp_recv\n");
    udp_recv(time_pcb, time_recv, (void*) 0);
    debug("time: connecting to time port\n");
    udp_connect(time_pcb, &s, TIME_PORT);

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
}

