
#include <nfs/nfs.h>

#include "rpc.h"
#include "time.h"
#include "pbuf_helpers.h"
#include "common.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <assert.h>

#include <sel4/sel4.h>
#include <pawpaw.h>
#include <network.h>

#define DEBUG_RPC 1
#ifdef DEBUG_RPC
#define debug(x...) printf( x )
#else
#define debug(x...)
#endif

extern seL4_CPtr msg_ep;
extern seL4_CPtr net_ep;

/************************************************************
 *  Constants
 ***********************************************************/
#define SRPC_VERSION 2

/* AUTH_UNIX: 9.2 http://tools.ietf.org/html/rfc1057            *
 * 'stamp' is arbitrary ID which the caller machine may generat */
#define AUTH_STAMP 37
#define ROOT 0
#define NFS_MACHINE_NAME "boggo"


#define CALL_TIMEOUT_MS 10
#define CALL_RETRIES     5

#define ROOT_PORT_MIN 45
#define ROOT_PORT_MAX 1024

#define UDP_PAYLOAD 1400

#define RETRANSMIT_DELAY_MS 500


/************************************************************
 *  Structures
 ***********************************************************/

typedef uint32_t xid_t;

struct prog_mismatch {
    uint32_t low;
    uint32_t high;
};


typedef struct auth_hdr {
    uint32_t flavour;
    uint32_t size;
    /* opaque body<400>; */
} auth_t;

typedef struct rpc_call_hdr {
    uint32_t xid;
    uint32_t type;
    uint32_t rpcvers;
    uint32_t prog;
    uint32_t vers;
    uint32_t proc;
    /* opaque_auth  cred; */
    /* opaque_auth  verf; */
    /* procedure specific data */
} call_body_hdr_t;

enum accept_stat {
    SUCCESS       = 0, // RPC executed successfully
    PROG_UNAVAIL  = 1, // remote hasn't exported program
    PROG_MISMATCH = 2, // remote can't support version #
    PROC_UNAVAIL  = 3, // program can't support procedure
    GARBAGE_ARGS  = 4  // procedure can't decode params
};

enum auth_flavor {
    AUTH_NULL       = 0,
    AUTH_UNIX       = 1,
    AUTH_SHORT      = 2,
    AUTH_DES        = 3
    /* and more to be defined */
};

/************************************************************
 *  Helpers
 ***********************************************************/
#define ROUNDDOWN(v, r) ((v) - ((v) & ((r) - 1)))
#define ROUNDUP(v, r)   ROUNDDOWN(v + (r) - 1, r)

static inline struct pbuf*
pbuf_new(u16_t length)
{
    return pbuf_alloc(PBUF_TRANSPORT, length, PBUF_RAM);
}

static inline enum rpc_stat
my_udp_send(int connection_id, struct pbuf *pbuf)
{
    /* FIXME: should use same share to recv? or share pool */
    struct pawpaw_share* share = pawpaw_share_new ();
    assert (share);

    struct pbuf *p = pbuf_new(pbuf->tot_len);
    if(p == NULL) {
        return RPCERR_NOBUF;
    }

    assert(!pbuf_copy(p, pbuf));

    /* read data out of pbuf into our regular buffer
     * TODO: yes, this is inefficient, but it means i don't have to re-write
     * all the pbuf stuff that this code uses... */
    /* FIXME: make sure we don't overrun our buffer! */
    struct pbuf *q;
    int offset = 0;
    for (q = p; q != NULL; q = q->next) {
        char* data = q->payload;
        memcpy (share->buf + offset, data, q->len);
        for (int i = 0; i < q->len && i < 500; i++) {
            printf ("out_data[%d] = 0x%x\n", offset + i, ((char*)(share->buf))[offset + i]);
        }
        offset += q->len;
    }

    debug ("loaded pbuf into cap, sending to svc_net (len 0x%x)...\n", offset);
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, share->cap);
    seL4_SetMR (0, NETSVC_SERVICE_SEND);
    seL4_SetMR (1, share->id);
    seL4_SetMR (2, connection_id);
    seL4_SetMR (3, p->tot_len);
    seL4_Call (net_ep, msg);

    debug ("got result %d\n", seL4_GetMR (0));
    if (seL4_GetMR (0) == 0) {
        return RPC_OK;
    } else {
        return RPCERR_COMM;
    }

    /* pbuf gets freed on successful recv or failure */
}

/************************************************************
 *  Transaction ID's
 ***********************************************************/
static xid_t cur_xid = 100;

static xid_t
extract_xid(struct pbuf *pbuf)
{
    xid_t xid;
    int pos = 0;
    /* extract the xid */
    pb_readl(pbuf, &xid, &pos);
    return xid;
}

static void
seed_xid(uint32_t seed)
{
    cur_xid = seed * 10000;
}

static xid_t
get_xid(void)
{
    return ++cur_xid;
}


/************************************************************
 *  Mailboxes
 ***********************************************************/

struct rpc_queue {
    int conn_id;
    struct pbuf *pbuf;
    xid_t xid;
    int timeout;
    struct rpc_queue *next;
    void (*func) (void *, uintptr_t, struct pbuf *);
    void *callback;
    uintptr_t arg;
};

struct rpc_queue *queue = NULL;

/* 
 * Poll to see if packets should be resent.
 * Packet loss can be simulated using the following command on the
 * server:
 * sudo tc qdisc add dev eth0 root netem loss 50%
 * sudo tc qdisc show dev eth0
 * sudo tc qdisc del dev eth0 root netem loss 50%
 */
void
rpc_timeout(int ms)
{
    struct rpc_queue *q_item;
    for (q_item = queue; q_item != NULL; q_item = q_item->next) {
        q_item->timeout += ms;
        if (q_item->timeout > RETRANSMIT_DELAY_MS) {
            debug("rpc_timeout: Retransmission of 0x%08x\n", q_item->xid);
            if(my_udp_send(q_item->conn_id, q_item->pbuf)){
                /* Try again later */
            }else{
                q_item->timeout = 0;
            }
        }
    }
}


static void
add_to_queue(struct pbuf *pbuf, int conn_id,
         void (*func)(void *, uintptr_t, struct pbuf *),
         void *callback, uintptr_t arg)
{
    /* Need a lock here */
    struct rpc_queue *q_item;
    struct rpc_queue *tmp;
    q_item = malloc(sizeof(struct rpc_queue));
    assert(q_item != NULL);

    q_item->next = NULL;
    q_item->pbuf = pbuf;
    q_item->xid = extract_xid(pbuf);
    debug ("queue: adding to queue with xid 0x%x\n", q_item->xid);
    q_item->conn_id = conn_id;
    q_item->timeout = 0;
    q_item->func = func;
    q_item->arg = arg;
    q_item->callback = callback;

    if (queue == NULL) {
        /* Add at start of the linked list */
        queue = q_item;
    } else {
        /* Add to end of the linked list */
        for(tmp = queue; tmp->next != NULL; tmp = tmp->next)
            ;
        tmp->next = q_item;
    }
}

/* Remove item from the queue -- doesn't free the memory */
static struct rpc_queue *
get_from_queue(xid_t xid)
{
    debug ("queue: asking to fetch xid 0x%x\n", xid);
    struct rpc_queue *tmp, *last = NULL;

    for (tmp = queue; tmp != NULL && tmp->xid != xid; tmp = tmp->next) {
        last = tmp;
        ;
    }
    if (tmp == NULL) {
        return NULL;
    } else if (last == NULL) {
        queue = tmp->next;
    } else {
        last->next = tmp->next;
    }

    return tmp;
}

/**********************************
 *** RPC transport
 **********************************/

/* FIXME: owner app should call this on receiving message */
int
my_recv(void *arg, int upcb, struct pbuf *p,
    struct ip_addr *addr, u16_t port)
{
    xid_t xid;
    struct rpc_queue *q_item;
    (void)port;

    xid = extract_xid(p);
    debug("my_recv: had xid 0x%x\n", xid);

    q_item = get_from_queue(xid);

    debug("Received a reply for xid: %u (%d) %p\n", xid, p->len, q_item);
    if (q_item != NULL){
        assert(q_item->func);
        q_item->func(q_item->callback, q_item->arg, p);
        /* Clean up the queue item */
        pbuf_free(q_item->pbuf);
        free(q_item);
        return true;
    }
    /* Done with the incoming packet so free it */
    pbuf_free(p);
    return false;
}


enum rpc_stat
rpc_send(struct pbuf *pbuf, int len, int pcb,
     void (*func)(void *, uintptr_t, struct pbuf *), 
     void *callback, uintptr_t token)
{
    assert(pcb);
    pbuf_realloc(pbuf, len);
    /* Add to a queue */
    add_to_queue(pbuf, pcb, func, callback, token);
    return my_udp_send(pcb, pbuf);
}

struct rpc_call_arg {
    void (*func)(void *, uintptr_t, struct pbuf *);
    uintptr_t token;
    void* callback;
    _mutex mutex;
};

static void
rpc_call_cb(void *callback, uintptr_t token, struct pbuf *pbuf)
{
    struct rpc_call_arg *call_arg = (struct rpc_call_arg*)token;
    (void)callback;
    debug("Signal function called\n");
    assert(call_arg);

    call_arg->func(call_arg->callback, call_arg->token, pbuf);

    _mutex_release(call_arg->mutex);
}

/* TODO: rename rpc_call-> rpc_call_blocking or something to emphasise non-async */
enum rpc_stat
rpc_call(struct pbuf *pbuf, int len, int handler_id, 
     void (*func)(void *, uintptr_t, struct pbuf *), 
     void *callback, uintptr_t token)
{
    struct rpc_call_arg call_arg;
    //struct rpc_queue *q_item;
    //int time_out;
    enum rpc_stat stat;
    //xid_t xid;

    /* If we give up early, we must ensure that the argument remains in memory
     * just in case the packet comes in later */
    assert(pbuf);

    /* GeneratrSend the thing with the unlock frunction as a callback */
    call_arg.func = func;
    call_arg.callback = callback;
    call_arg.token = token;
    call_arg.mutex = _mutex_create();
    _mutex_acquire(call_arg.mutex);

    /* Make the call */
    //xid = extract_xid(pbuf);
    debug ("doing rpc_send...\n");
    stat = rpc_send(pbuf, pbuf->tot_len, handler_id, &rpc_call_cb, NULL, 
                   (uintptr_t)&call_arg);
    if(stat){
        debug ("=> failed with err %d\n", stat);
        return stat;
    }

    /* Wait for the response here instead of the main loop doing it */
    /* FIXME: what about timeout */
#if 0
    time_out = RETRANSMIT_DELAY_MS * (CALL_RETRIES + 1);
    while(time_out >= 0){
        _usleep(CALL_TIMEOUT_MS * 1000);
        if(_mutex_try_acquire(call_arg.mutex)){
            /* Success */
            _mutex_destroy(call_arg.mutex);
            return 0;
        }
        rpc_timeout(CALL_TIMEOUT_MS);
        time_out -= CALL_TIMEOUT_MS;
    }
#endif
    seL4_Wait (msg_ep, NULL);
    int id = seL4_GetMR (0);    /* FIXME: might be ORed */
    debug ("got reply on conn %d\n", id);

    /* ok had data, go fetch it */
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, NETSVC_SERVICE_DATA);
    seL4_SetMR (1, id);

    debug ("asking for data for connection %d\n", id);
    seL4_Call (net_ep, msg);
    seL4_Word size = seL4_GetMR(0);
    debug ("had 0x%x bytes data\n", size);

    struct pawpaw_share* result_share = pawpaw_share_mount (pawpaw_event_get_recv_cap ());
    assert (result_share);

    /* create pbuf: FIXME maybe need a local copy rather than ref? */
    struct pbuf* recv_pbuf = pbuf_alloc (PBUF_TRANSPORT, size, PBUF_REF);
    assert (recv_pbuf);
    recv_pbuf->payload = result_share->buf;
    assert (recv_pbuf->tot_len == size);

    /* handle it */
    //debug ("calling recv function\n");
    /* FIXME: unmount share after recv */
    return my_recv (NULL, id, recv_pbuf, NULL, 0) == true;

    //return 0;

#if 0
    /* If we get here, we have failed. Data is on the stack so make sure 
     * we remove references from the queue */
    q_item = get_from_queue(xid);
    assert(q_item);
    pbuf_free(q_item->pbuf);
    free(q_item);
    _mutex_destroy(call_arg.mutex);
    return RPCERR_COMM;
#endif
}


/************************************************************
 * Initialisation 
 ***********************************************************/

int
init_rpc(const struct ip_addr *server)
{
    /* FIXME: make this async EP instead to get rid of hack - huhh??? */
    msg_ep = pawpaw_create_ep ();
    if (!msg_ep) {
        return false;
    }

    uint32_t time;
    time = udp_time_get(server);
    seed_xid(time);
    return time == 0;
}

int
rpc_new_udp(const struct ip_addr *server, int remote_port, 
            enum port_type local_port)
{
    seL4_MessageInfo_t msg;

    msg = seL4_MessageInfo_new (0, 0, 1, 4);
    seL4_SetCap (0, msg_ep);
    seL4_SetMR (0, NETSVC_SERVICE_REGISTER);
    seL4_SetMR (1, NETSVC_PROTOCOL_UDP);
    seL4_SetMR (2, remote_port);    /* FIXME: what about binding local port */
    seL4_SetMR (3, 0);
    // FIXME: implement this
    //seL4_SetMR (3, (seL4_Word)s[0]);  /* phew, this fits into u32 */

    seL4_Call (net_ep, msg);
    return seL4_GetMR (0);

#if 0
    struct udp_pcb* ret;
    static int root_port = -1;
    struct ip_addr s = *server;
    ret = udp_new();
    assert(ret);
    udp_recv(ret, my_recv, NULL);
    if(local_port == PORT_ROOT){
        if(root_port >= ROOT_PORT_MAX || root_port < ROOT_PORT_MIN){
            root_port = ROOT_PORT_MIN;
            debug("Recycling ports\n");
        }
        udp_bind(ret, IP_ADDR_ANY, root_port++);
    }else{
        /* let lwip decide for itself */
    }
    udp_connect(ret, &s, remote_port);
    return ret;
#endif
}


enum rpc_reply_err
rpc_read_hdr(struct pbuf* pbuf, struct rpc_reply_hdr* hdr, int* pos)
{
    *pos = 0;
    /* read in the header */
    pb_read_arrl(pbuf, (uint32_t*)hdr, sizeof(*hdr), pos);
    if(hdr->msg_type != MSG_REPLY){
        debug( "Got a reply to something else!!\n" );
        debug( "Looking for msgtype %d\n", MSG_REPLY );
        debug( "Got msgtype %d\n", hdr->msg_type);
        return RPCERR_BAD_MSG;

    }else if(hdr->reply_stat != MSG_ACCEPTED){
        uint32_t err;
        debug( "Message NOT accepted (%d)\n", hdr->reply_stat);
        /* extract error code */
        pb_readl(pbuf, &err, pos);
        debug( "Error code %d\n", err);
        if(err == 1) {
            /* get the auth problem */
            pb_readl(pbuf, &err, pos);
            debug( "auth_stat %d\n", err);
        }
        return RPCERR_NOT_ACCEPTED;

    }else{
        auth_t auth;
        uint32_t auth_stat;

        /* parse the auth field */
        pb_read_arrl(pbuf, (uint32_t*)&auth, sizeof(auth), pos);
        assert(auth.flavour == AUTH_NULL);
        debug("Got auth data. size is %d\n", auth.size);

        /* parse the auth status */
        pb_readl(pbuf, &auth_stat, pos);
        if(auth_stat == SUCCESS){
            return RPCERR_OK;
        }else{
            debug( "reply stat was %d\n", auth_stat);
            return RPCERR_FAILURE;
        }
    }
}

static void
rpc_write_hdr(struct pbuf* pbuf, int prog, int vers, int proc, int* pos)
{
    /** Construct header **/
    /* The rpc call header */
    struct rpc_call_hdr rpc_hdr = {
            .xid     = get_xid(),
            .type    = MSG_CALL,
            .rpcvers = SRPC_VERSION,
            .prog    = prog,
            .vers    = vers,
            .proc    = proc
        };
    /* Credentials */
    const char* host = NFS_MACHINE_NAME;
    uint32_t ids[] = {ROOT, ROOT, ROOT};
    struct auth_hdr cred = {
            .flavour = AUTH_UNIX,
            .size    = ROUNDUP(sizeof(uint32_t)/* stamp */+
                               sizeof(uint32_t)/* hostlen */ + strlen(host) +
                               sizeof(ids), 4)
        };
    struct auth_hdr verif = {
            .flavour = AUTH_NULL,
            .size    = 0
        };

    /** Dump to pbuf **/
    *pos = 0;
    /* Header */
    pb_write_arrl(pbuf, (uint32_t*)&rpc_hdr, sizeof(rpc_hdr), pos);
    /* Credentials. Variable host name makes it hard to struct */
    pb_write_arrl(pbuf, (uint32_t*)&cred, sizeof(cred), pos);
    pb_writel(pbuf, AUTH_STAMP, pos);
    pb_write_str (pbuf, host, strlen(host), pos);
    pb_write_arrl(pbuf, ids, sizeof(ids), pos);
    /* Verifier */
    pb_write_arrl(pbuf, (uint32_t*)&verif, sizeof(verif), pos);
}

struct pbuf *
rpcpbuf_init(int prognum, int vernum, int procnum, int* pos)
{
    struct pbuf* pbuf;
    pbuf = pbuf_alloc(PBUF_TRANSPORT, UDP_PAYLOAD, PBUF_RAM);
    if(pbuf) {
        rpc_write_hdr(pbuf, prognum, vernum, procnum, pos);
    }
    return pbuf;
}


