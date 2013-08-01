/* @Author Alexander Kroh */

#include <ethdrivers/raw_iface.h>
#include "legacy_desc.h"
#include "../../os_iface.h"
#include <assert.h>
#include <stdio.h>
#include "io.h"


#define BIT(x)  (1UL << (x))

/* Receive descriptor status */ 
#define RXD_EMPTY     BIT(15) /* Buffer has no data. Waiting for reception. */
#define RXD_OWN0      BIT(14) /* Receive software ownership. R/W by user */
#define RXD_WRAP      BIT(13) /* Next buffer is found in ENET_RDSR */
#define RXD_OWN1      BIT(12) /* Receive software ownership. R/W by user */
#define RXD_LAST      BIT(11) /* Last buffer in frame. Written by the uDMA. */
#define RXD_MISS      BIT( 8) /* Frame does not match MAC (promiscuous mode) */
#define RXD_BROADCAST BIT( 7) /* frame is a broadcast frame */
#define RXD_MULTICAST BIT( 6) /* frame is a multicast frame */
#define RXD_BADLEN    BIT( 5) /* Incoming frame was larger than RCR[MAX_FL] */
#define RXD_BADALIGN  BIT( 4) /* Frame length does not align to a byte */
#define RXD_CRCERR    BIT( 2) /* The frame has a CRC error */
#define RXD_OVERRUN   BIT( 1) /* FIFO overrun */
#define RXD_TRUNC     BIT( 0) /* Receive frame > TRUNC_FL */

#define RXD_ERROR    (RXD_BADLEN  | RXD_BADALIGN | RXD_CRCERR |\
                      RXD_OVERRUN | RXD_TRUNC)

/* Transmit descriptor status */ 
#define TXD_READY     BIT(15) /* buffer in use waiting to be transmitted */
#define TXD_OWN0      BIT(14) /* Receive software ownership. R/W by user */
#define TXD_WRAP      BIT(13) /* Next buffer is found in ENET_TDSR */
#define TXD_OWN1      BIT(12) /* Receive software ownership. R/W by user */
#define TXD_LAST      BIT(11) /* Last buffer in frame. Written by the uDMA. */
#define TXD_ADDCRC    BIT(10) /* Append a CRC to the end of the frame */
#define TXD_ADDBADCRC BIT( 9) /* Append a bad CRC to the end of the frame */


struct ldesc_ring {
    struct dma_addr ring;
    struct dma_addr* buf;
    struct descriptor* ld;
    int count;
    int bufsize;
    int head;
    int tail;
    int unused;
};

struct ldesc {
    struct ldesc_ring rx;
    struct ldesc_ring tx;
    /* buffer pool. acts as a queue to try and optimize cache usage */
    dma_addr_t *pool_queue;
    int queue_index;
    int pool_size;
    int buf_size;
};

struct descriptor {
    /* NOTE: little endian packing: len before stat */
#if BYTE_ORDER == LITTLE_ENDIAN
    uint16_t len;
    uint16_t stat;
#elif BYTE_ORDER == BIG_ENDIAN
    uint16_t stat;
    uint16_t len;
#else
#error Could not determine endianess
#endif
    uint32_t phys;
};

static void free_dma_buf(struct ldesc *ldesc, dma_addr_t buf) {
    assert(ldesc);
    assert(ldesc->queue_index > 0);
    ldesc->queue_index--;
    ldesc->pool_queue[ldesc->queue_index] = buf;
}

static dma_addr_t alloc_dma_buf(struct ldesc *ldesc) {
    dma_addr_t ret;
    if (ldesc->queue_index == ldesc->pool_size) return (dma_addr_t){0, 0};
    ret = ldesc->pool_queue[ldesc->queue_index];
    ldesc->queue_index++;
    return ret;
}

static int fill_dma_pool(struct ldesc *ldesc, int count, int bufsize) {
    int i;
    ldesc->pool_queue = os_malloc(sizeof(*(ldesc->pool_queue)) * count);
    if (!ldesc->pool_queue) return -1;
    for (i = 0; i < count; i++) {
        ldesc->pool_queue[i] = os_dma_malloc(bufsize, 1);
        if (!os_dma_valid(&ldesc->pool_queue[i])) {
            return -1;
        }
    }
    ldesc->queue_index = 0;
    ldesc->pool_size = count;
    ldesc->buf_size = bufsize;
    return 0;
}

static int create_ring(struct ldesc_ring *r, int count, int bufsize) {
    /* create the buffer list */
    r->buf = (struct dma_addr*)os_malloc(sizeof(struct dma_addr) * count);
    if(r->buf == NULL){
        return -1;
    }
    /* Create the descriptor ring */
    r->ring = os_dma_malloc(sizeof(struct descriptor) * count, 0);
    if(!os_dma_valid(&r->ring)){
        free(r->buf);
        return -1;
    }
    /* Initialise the ring */
    r->ld = (struct descriptor*)os_virt(&r->ring);
    r->count = count;
    r->bufsize = bufsize;
    return 0;
}

static int
fill_ring(struct ldesc_ring* r){
    int i;
    /* Create the buffers */
    for(i = 0; i < r->count; i++){
        r->buf[i] = os_dma_malloc(r->bufsize, 1);
        if(!os_dma_valid(&r->buf[i])){
            return -1;
        }
    }
    return 0;
}

struct ldesc*
ldesc_init(int rx_count, int rx_size, int tx_count, int tx_size){
    struct ldesc * d;
    int error;
    (void)error;
    d = (struct ldesc*)os_malloc(sizeof(struct ldesc));
    assert(d);
    error = create_ring(&d->rx, rx_count, rx_size);
    assert(!error);
    error = fill_ring(&d->rx);
    assert(!error);
    error = create_ring(&d->tx, tx_count, tx_size);
    assert(!error);
    error = fill_dma_pool(d, tx_count, tx_size);
    assert(!error);
    ldesc_reset(d);
    return d;
}

void
ldesc_reset(struct ldesc* ldesc){
    struct descriptor *d;
    int i;
    /* reset head and tail */
    ldesc->rx.head = 0;
    ldesc->rx.tail = 0;
    ldesc->rx.unused = 0;
    ldesc->tx.head = 0;
    ldesc->tx.tail = 0;
    ldesc->tx.unused = ldesc->tx.count;
    /* clear TX descriptor data */
    d = ldesc->tx.ld;
    for(i = 0; i < ldesc->tx.count; i++){
        d[i].stat = 0;
        d[i].phys = 0;
        d[i].len = 0;
    }
    d[ldesc->tx.count - 1].stat |= TXD_WRAP;
    os_clean(os_virt(&ldesc->tx.ring), sizeof(*d) * ldesc->tx.count);
    /* Clear RX descriptor data */
    d = ldesc->rx.ld;
    for(i = 0; i < ldesc->rx.count; i++){
        d[i].phys = (uint32_t)os_phys(&ldesc->rx.buf[i]);;
        d[i].stat = RXD_EMPTY;
        d[i].len = 0;
    }
    d[ldesc->rx.count - 1].stat |= RXD_WRAP;
    os_clean(os_virt(&ldesc->rx.ring), sizeof(*d) * ldesc->rx.count);
}

struct ldesc_data 
ldesc_get_ringdata(struct ldesc* ldesc){
    struct ldesc_data d;
    d.tx_phys = (uint32_t)os_phys(&ldesc->tx.ring);
    d.tx_bufsize = ldesc->tx.bufsize;
    d.rx_phys = (uint32_t)os_phys(&ldesc->rx.ring);
    d.rx_bufsize = ldesc->rx.bufsize;
    return d;
}


int
ldesc_txget(struct ldesc* ldesc, dma_addr_t* buf){
    int i;
    assert(ldesc);
    assert(buf);
    i = ldesc->tx.tail;
    /* Clean stale buffers so we can try to reuse them */
    ldesc_txcomplete(ldesc);
    if (ldesc->tx.unused == 0) {
        return 0;
    }
    assert(! (ldesc->tx.ld[i].stat & TXD_READY) );
    dma_addr_t dma_buf = alloc_dma_buf(ldesc);
    assert(os_dma_valid(&dma_buf));
    *buf = dma_buf;
    ldesc->tx.buf[i] = dma_buf;
    return ldesc->buf_size;
}

int
ldesc_txput(struct ldesc* ldesc, dma_addr_t buf, int len){
    int i;
    struct descriptor* d;
    i = ldesc->tx.tail;
    d = &ldesc->tx.ld[i];
    assert(ldesc->tx.unused > 0);
    assert(!(d->stat & TXD_READY));
    assert(os_phys(&ldesc->tx.buf[i]) == os_phys(&buf));
    assert(os_virt(&ldesc->tx.buf[i]) == os_virt(&buf));
    /* Clean the buffer out to RAM */
    os_clean(os_virt(&buf), len);
    /* Set the descriptor */
    d->len = len;
    d->phys = (uint32_t)os_phys(&buf);
    /* Adjust for next index */
    ldesc->tx.tail++;
    ldesc->tx.unused--;
    if(ldesc->tx.tail == ldesc->tx.count){
        d->stat = TXD_WRAP | TXD_LAST | TXD_ADDCRC;
        ldesc->tx.tail = 0;
    } else {
        d->stat = TXD_LAST | TXD_ADDCRC;
    }
    /* Must ensure changes are observable before setting ready bit */
    dmb();
    d->stat |= TXD_READY;
    /* Must ensure changes are observable before signalling the MAC */
    dsb();
    return 0;
}


int
ldesc_rxget(struct ldesc* ldesc, dma_addr_t* buf, int* len){
    int i;
    struct descriptor* d;
    i = ldesc->rx.tail;
    d = &ldesc->rx.ld[i];
    if(d->stat & RXD_EMPTY){
        return 0;
    }else{
        *buf = ldesc->rx.buf[i];
        *len = d->len;
        if(d->stat & RXD_ERROR){ 
            return -1;
        }else{
            return 1;
        }
    }
}

void
ldesc_rxput(struct ldesc* ldesc, dma_addr_t dma){
    int i;
    struct descriptor* d;
    i = ldesc->rx.tail;
    d = &ldesc->rx.ld[i];
    assert(!(d->stat & RXD_EMPTY));
    /* Update descriptor */
    assert(os_phys(&ldesc->rx.buf[i]) == os_phys(&dma));
    assert(os_virt(&ldesc->rx.buf[i]) == os_virt(&dma));
//    ldesc->rx.buf[i] = dma;
    d->len = ldesc->rx.bufsize;
    /* this assert seems to cause breakage. presumably due to the read of 'phys'.
     * the cause of this is a mystery :( */
//    assert(d->phys == (uint32_t)os_phys(&dma));
//    d->phys = (uint32_t)os_phys(&dma);
    /* Invalidate the buffer */
    os_invalidate(os_virt(&dma), d->len);
    /* Adjust our index */
    ldesc->rx.tail++;
    if(ldesc->rx.tail == ldesc->rx.count){
//        d->stat = RXD_WRAP;
        ldesc->rx.tail = 0;
    }
    /* Ensure descriptor changes are observable before giving it to DMA */
    dmb();
    d->stat |= RXD_EMPTY;
    dsb();
}



void 
ldesc_print(struct ldesc* ldesc){
    struct descriptor* d;
    int i;
    /* print RX descriptors */
    d = ldesc->rx.ld;
    printf("RX Descriptors: ring base 0x%x\n", (uint32_t)d);
    for(i = 0; i < ldesc->rx.count; i++, d++){
        putchar((i == ldesc->rx.tail)? '*' : ' ');
        printf("%02d | s0x%04x p0x%08x l0x%04x | %s\n",
                i, d->stat, d->phys, d->len,
                (d->stat & RXD_EMPTY)? "Empty" : "Full");
    }
    /* print TX descriptors */
    d = ldesc->tx.ld;
    printf("TX Descriptors: ring base 0x%x\n", (uint32_t)d);
    for(i = 0; i < ldesc->tx.count; i++, d++){
        putchar((i == ldesc->tx.tail)? '*' : ' ');
        printf("%02d | s0x%04x p0x%08x l0x%04x | %s\n",
                i, d->stat, d->phys, d->len, 
                (d->stat & TXD_READY)? "Queued" : "Available");
    }
}

int
ldesc_txcomplete(struct ldesc* ldesc){
    struct descriptor* d;
    int i;
    i = ldesc->tx.head;
    d = ldesc->tx.ld;
    for (i = ldesc->tx.head, d = ldesc->tx.ld
         ; ldesc->tx.unused < ldesc->tx.count && !(d[i].stat & TXD_READY)
         ; i = (i + 1) % ldesc->tx.count) {
//        assert(d[i].phys == (uint32_t)os_phys(&ldesc->tx.buf[i]));
        free_dma_buf(ldesc, ldesc->tx.buf[i]);
        ldesc->tx.unused++;
    }
    ldesc->tx.head = i;
    return ldesc->tx.unused == ldesc->tx.count;
}
