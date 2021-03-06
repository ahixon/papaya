#include "mountd.h"
#include "portmapper.h"
#include "rpc.h"
#include "pbuf_helpers.h"

#include <string.h>
#include <assert.h>

//#define DEBUG_MNT 1
#ifdef DEBUG_MNT
#define debug(x...) printf(x)
#else
#define debug(x...)
#endif


#define MNT_NUMBER    100005
#define MNT_VERSION   1

#define MNTPROC_EXPORT 5
#define MNTPROC_MNT    1

/*
 * The maximum number of bytes of data in a READ or WRITE
 * request.
 */
#define MAXDATA    8192

/* The maximum number of bytes in a pathname argument. */
#define MAXPATHLEN 1024
/* The size in bytes of the opaque "cookie" passed by READDIR. */
#define COOKIESIZE    4


/******************************************
 *** Helpers
 ******************************************/

static int mnt_pcb = -1;

static int
mnt_new_udp(const struct ip_addr *server)
{
    /* this is quite ineffient, getting the port all the time..
     * let's just assume we never connect to another server */

    if (mnt_pcb < 0) {
        int port = portmapper_getport(server, MNT_NUMBER, MNT_VERSION);
        mnt_pcb = rpc_new_udp(server, port, PORT_ROOT);
    }

    return mnt_pcb;
}



/******************************************
 *** Exports
 ******************************************/
struct mountd_exports_token {
    enum rpc_reply_err err;
};

static void 
mountd_print_exports_cb(void* callback, uintptr_t token, struct pbuf *pbuf)
{
    struct mountd_exports_token *t = (struct mountd_exports_token*)token;
    struct rpc_reply_hdr reply_hdr;
    int pos;
    (void)callback;
    t->err = rpc_read_hdr(pbuf, &reply_hdr, &pos);
    if(t->err == RPCERR_OK){
        uint32_t opt;

        /* Read the returned export list */
        while (pb_readl(pbuf, &opt, &pos), opt){
            char str[100];

            /* Read the mount point */
            printf( "NFS Export...\n" );
            pb_read_str(pbuf, str, sizeof(str), &pos);
            printf( "* Export name is %s\n", (char*) &str );

            /* Read additional details */
            while (pb_readl(pbuf, &opt, &pos), opt){
                pb_read_str(pbuf, str, sizeof(str), &pos);
                printf("* Group %s\n", str);
            }
        }
    }
}


enum rpc_stat
mountd_print_exports(const struct ip_addr *server)
{
    struct mountd_exports_token token;
    struct pbuf *pbuf;
    int pos;
    int err;

    /* open a port */
    mnt_pcb = mnt_new_udp(server); 
    assert(mnt_pcb);

    /* construct the call */
    pbuf = rpcpbuf_init(MNT_NUMBER, MNT_VERSION, MNTPROC_EXPORT, &pos);
    assert(pbuf != NULL);

    /* Make the call */
    err = rpc_call(pbuf, pos, mnt_pcb, &mountd_print_exports_cb, NULL, 
                    (uintptr_t)&token);
    /* FIXME: free our connection */

    /* Process response */
    if(err){
        debug("mountd: rpc call failed\n");
        return err;
    }else if(token.err){
        debug("mountd: server error\n");
        return RPCERR_NOSUP;
    }else{
        return RPC_OK;
    }
}


/******************************************
 *** Mount
 ******************************************/

struct mountd_mnt_token {
    enum rpc_stat stat;
    const char* dir;
    fhandle_t *pfh;
};

static void
mountd_mount_cb(void* callback, uintptr_t token, struct pbuf* pbuf)
{
    struct mountd_mnt_token* t = (struct mountd_mnt_token*)token;
    struct rpc_reply_hdr reply_hdr;
    enum rpc_reply_err err;
    int pos;

    (void)callback;

    err = rpc_read_hdr(pbuf, &reply_hdr, &pos);
    if(err == RPCERR_OK){
        uint32_t status;
        /* Read the response */
        pb_readl(pbuf, &status, &pos);
        if (status == 0) {
            pb_read(pbuf, t->pfh, sizeof(*(t->pfh)), &pos);
            t->stat = RPC_OK;
        }else{
            t->stat = RPCERR_NOSUP;
        }
    }else{
        t->stat = RPCERR_NOSUP;
    }
}

enum rpc_stat
mountd_mount(const struct ip_addr *server, const char *dir, fhandle_t *pfh)
{
    struct mountd_mnt_token token;
    int mnt_pcb;
    struct pbuf *pbuf;
    int pos;
    enum rpc_stat stat;

    /* open a port */
    mnt_pcb = mnt_new_udp(server); 
    assert(mnt_pcb);

    /* Construct the call */
    pbuf = rpcpbuf_init(MNT_NUMBER, MNT_VERSION, MNTPROC_MNT, &pos);
    if(pbuf == NULL){
        //udp_remove(mnt_pcb);
        /* FIXME: cleanup UDP client allocation */
        return RPCERR_NOBUF;
    }

    pb_write_str(pbuf, dir, strlen(dir), &pos);

    /* Make the call */
    token.pfh = pfh;
    token.stat = RPC_OK;
    stat = rpc_call(pbuf, pos, mnt_pcb, &mountd_mount_cb, NULL, (uintptr_t)&token);
    //udp_remove(mnt_pcb);
    /* FIXME: cleanup UDP client allocation */

    /* Process the returned value */
    if (stat != RPC_OK) {
        debug("mountd: RPC error mounting %s\n", dir);
        return stat;
    }else if(token.stat != RPC_OK){
        debug("mountd: server refused to mount %s\n", dir);
        return token.stat;
    }else {
        /* Done */
        debug("mountd: Mounted path %s\n", dir);
        return RPC_OK;
    }
}


