#include <ethdrivers/ethdriver_tests.h>
#include "plat/imx6/unimplemented.h"
#include <lwip/udp.h>
#include <assert.h>


#define PORT 7
#define CLIENT_SEED_MAX     111
#define CLIENT_LENGTH_MAX   1450

struct udp_pcb* spcb = NULL;
struct udp_pcb* cpcb = NULL;

/***** Server part *****/
static void
srecv(void *arg, struct udp_pcb *pcb, struct pbuf *p, 
        ip_addr_t *addr, u16_t port){
    int* token = (int*)arg;
    err_t err;
    err = udp_sendto(spcb, p, addr, port);
    switch(err){
    case ERR_OK:  *token = *token + 1; break;
    case ERR_MEM: printf("ERR: MEM\n"); break;
    case ERR_RTE: printf("ERR: RTE\n"); break;
    default: printf("Unknown err\n");
    }
    pbuf_free(p);
}

int 
udpecho_server(struct netif* netif){
    spcb = udp_new();
    assert(spcb);
    int arg;
    udp_recv(spcb, &srecv, &arg);
    assert(!udp_bind(spcb, IP_ADDR_ANY, PORT));
    return 0;
}


/***** Client part *****/

struct carg {
    int recv;
    int err;
};

static struct pbuf*
createbuf(int seed, int len){
    struct pbuf *q, *p;
    unsigned char* data;
    int i = 0;
    assert(len > 4);
    p = q = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
    assert(p);
    while(q != NULL){
        data = (unsigned char*)q->payload;
        int j;
        assert(data);
        for(j = 0; j < q->len; j++, data++){
            *data = (seed + i++) & 0xff;
        }
        q = q->next;
    }
    data = (unsigned char*)p->payload;
    data[0] = (seed >> 8) & 0xff;
    data[1] = (seed >> 0) & 0xff;
    data[2] = (len  >> 8) & 0xff;
    data[3] = (len  >> 0) & 0xff;
    return p;
}

static int 
checkandfreebuf(struct pbuf* p){
    /* check the packet */
    struct pbuf* q = p;
    char err_bits = 0;
    int bytes = 0;
    unsigned char* data;
    int seed;
    int len;
    int i = 0;

    /* Pull out the seed and packet length */
    data = (unsigned char*)p->payload;
    seed = data[0] << 0;
    seed <<= 8;
    seed += data[1];
    len = data[2];
    len <<= 8;
    len += data[3];
    assert(len == p->tot_len);

    /* Check the packet contents */
    while(q != NULL){
        data = (unsigned char*)q->payload;
        int j;
        assert(data);
        for(j = 0; j < q->len; j++, data++){
            err_bits |= *data ^ ((seed + i++) & 0xff);
            /* first 4 bytes not counted */
            if(bytes++ == 3){
                err_bits = 0;
            }
        }
        q = q->next;
    }
    pbuf_free(p);
    return err_bits;
}

static void
crecv(void *arg, struct udp_pcb *pcb, struct pbuf *p, 
        ip_addr_t *addr, u16_t port){
    struct carg* carg = (struct carg*)arg;
    int ebits;
    /* check the packet */
    carg->recv++;
    /* report on errors */
    ebits = checkandfreebuf(p);
    if(ebits != 0){
        printf("err bits= 0x%02x\n", ebits);
        carg->err++;
    }
}

/* 
 * Start a UDP echo client and return 
 */
#define BACK_TO_BACK 10
#define TIMEOUT 5000
int
udpecho_client(struct netif* netif, ip_addr_t *addr, 
                void (*sleep)(int ms)){
    struct carg carg;
    int i,j;

    /* Create the connection to the server */
    cpcb = udp_new();
    assert(cpcb);
    udp_recv(cpcb, &crecv, &carg);
    assert(!udp_connect(cpcb, addr, PORT));

    /* First make sure that the arp table is primed */
    {
        struct pbuf* q;
        q = createbuf(0,10);
        assert(q);
        carg.recv = 0;
        carg.err = 0;
        assert(!udp_send(cpcb,q));
        while(carg.recv == 0){
            sleep(1000);
        }
        pbuf_free(q);
    }

    /* Now perform the test */
    for(i = 1; i < 100; i++){
        /* GRRR xinetd udp echo does not handle multiple
         * requests even with "wait" set to "no". These
         * requests are dropped so we must wait for a
         * response first. b2b must be 1 until further notice.
         */
        int b2b = 1; 
        int timeout;
        carg.recv = 0;
        carg.err = 0;
        printf("iteration %d: sending %d packets back to back\n", i, b2b);
        for(j = 0; j < b2b; j++){
            struct pbuf* p;
            int seed = i;
            int len = i + 50;
            p = createbuf(seed, len);
            assert(p);
            assert(!udp_send(cpcb, p));
            pbuf_free(p);
        }
        printf("Waiting for responses");
        timeout = 0;
        while(carg.recv != b2b){
            sleep(10 * 1000);
            timeout += 10;
            if(timeout > TIMEOUT){
                break;
            }
            printf(".");
        }
        printf("received %d packets\n", carg.recv);
        if(carg.err){
            printf("Errors: %d\n", carg.err);
        }
    }

    return 0;
}


