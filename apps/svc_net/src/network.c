/****************************************************************************
 *
 *      $Id: network.c,v 1.1 2003/09/10 11:44:38 benjl Exp $
 *
 *      Description: Initialise the network stack and NFS library.
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include "network.h"

#include <autoconf.h>

#include <assert.h>
#include <string.h>
#include <stdio.h>

#include <nfs/nfs.h>
#include <lwip/init.h>
#include <netif/etharp.h>
#include <ethdrivers/lwip_iface.h>

#include <pawpaw.h>
#include <sos.h>

#include "dma.h"

#define IRQ_BIT(irq) (1 << ((irq) & 0x1f))

#ifndef SOS_NFS_DIR
#  ifdef CONFIG_SOS_NFS_DIR
#    define SOS_NFS_DIR CONFIG_SOS_NFS_DIR
#  else
#    define SOS_NFS_DIR "/var/tftpboot/alex"
#  endif
#endif

#define ARP_PRIME_TIMEOUT_MS     1000
#define ARP_PRIME_RETRY_DELAY_MS   10

static struct net_irq {
    int irq;
    seL4_IRQHandler cap;
} *_net_irqs = NULL;
static int _nirqs = 0;

static seL4_CPtr _irq_ep;

fhandle_t mnt_point = { { 0 } };

struct netif *_netif;

/*******************
 ***  OS support ***
 *******************/

static void* sos_malloc (void* cookie, uint32_t size) {
    (void)cookie;
    return malloc (size);
}

static void* sos_map_device (void* cookie, eth_paddr_t addr, int size) {
    (void)cookie;
    return pawpaw_map_device ((unsigned int)addr, size);
}

void sos_usleep (int usecs) {
    usleep (usecs);
   
    /* Handle pending network traffic */
    while (ethif_input (_netif));
}


/*******************
 *** IRQ handler ***
 *******************/
void network_irq (seL4_Word irq) {
    int i;

    /* skip if the network was not initialised */
    if (_irq_ep == 0){
        return;
    }

    /* Loop through network irqs until we find a match */
    for(i = 0; i < _nirqs; i++){
        if(irq & IRQ_BIT(_net_irqs[i].irq)){
            int err;
            ethif_handleIRQ(_netif, _net_irqs[i].irq);
            err = seL4_IRQHandler_Ack(_net_irqs[i].cap);
            assert(!err);
        }
    }
}

static seL4_CPtr enable_irq (int irq, seL4_CPtr aep) {
    seL4_CPtr cap;
    int err;

    /* Create an IRQ handler */
    cap = pawpaw_register_irq (irq);
    assert (cap);

    /* Assign to an end point */
    err = seL4_IRQHandler_SetEndpoint(cap, aep);
    assert (!err);

    /* Ack the handler before continuing */
    err = seL4_IRQHandler_Ack (cap);
    assert (!err);

    return cap;
}

/********************
 *** Network init ***
 ********************/

static void
network_prime_arp(struct ip_addr *gw){
    int timeout = ARP_PRIME_TIMEOUT_MS;
    struct eth_addr* eth;
    struct ip_addr* ip;
    while(timeout > 0){
        /* Send an ARP request */
        etharp_request(_netif, gw);
        /* Wait for the response */
        //printf ("Waiting for ARP response\n");
        sos_usleep(ARP_PRIME_RETRY_DELAY_MS * 1000);
        if(etharp_find_addr(_netif, gw, &eth, &ip) == -1){
            timeout += ARP_PRIME_RETRY_DELAY_MS;
        }else{
            return;
        }
    }
}

void 
network_init(seL4_CPtr interrupt_ep) {
    struct ip_addr netmask, ipaddr, gw;
    struct eth_driver* eth_driver;
    const int* irqs;
    int err;
    int i;

    struct ethif_os_interface sos_interface = {
            .cookie = NULL,
            .clean = &dma_clean,
            .invalidate = &dma_invalidate,
            .dma_malloc = &sos_dma_malloc,
            .malloc = &sos_malloc,
            .ioremap = &sos_map_device
        };

    _irq_ep = interrupt_ep;

    /* Extract IP from .config */
    //printf("\nInitialising network...\n\n");
    err = 0;
    err |= !ipaddr_aton(CONFIG_SOS_GATEWAY,      &gw);
    err |= !ipaddr_aton(CONFIG_SOS_IP     ,  &ipaddr);
    err |= !ipaddr_aton(CONFIG_SOS_NETMASK, &netmask);
    assert (!err);
    //conditional_panic(err, "Failed to parse IP address configuration");
    //printf("  Local IP Address: %s\n", ipaddr_ntoa( &ipaddr));
    //printf("Gateway IP Address: %s\n", ipaddr_ntoa(     &gw));
    //printf("      Network Mask: %s\n", ipaddr_ntoa(&netmask));
    //printf("\n");

    /* low level initialisation */
    eth_driver = ethif_plat_init(0, sos_interface);
    assert(eth_driver);

    /* Initialise IRQS */
    // printf ("Initialising IRQs\n");
    irqs = ethif_enableIRQ(eth_driver, &_nirqs);
    _net_irqs = (struct net_irq*)calloc(_nirqs, sizeof(*_net_irqs));
    for(i = 0; i < _nirqs; i++){
        _net_irqs[i].irq = irqs[i];
        // printf ("\tenabling IRQ for %d\n", irqs[i]);
        _net_irqs[i].cap = enable_irq(irqs[i], _irq_ep);
    }

    /* Setup the network interface */
    // printf ("Initialising LWIP\n");
    lwip_init();
    _netif = (struct netif*)malloc(sizeof(*_netif));
    assert(_netif != NULL);
    // printf ("adding interface\n");
    _netif = netif_add(_netif, &ipaddr, &netmask, &gw, 
                       eth_driver, ethif_init, ethernet_input);
    assert(_netif != NULL);
    // printf ("enabling interface\n");
    netif_set_up(_netif);
    // printf ("setting interface as default\n");
    netif_set_default(_netif);

    /*
     * LWIP does not queue packets while waiting for an ARP response 
     * Generally this is okay as we block waiting for a response to our
     * request before sending another. On the other hand, priming the
     * table is cheap and can save a lot of heart ache 
     */
    // printf ("priming ARP table\n");
    network_prime_arp(&gw);

    /* initialise and mount NFS */
#if 0
    if(strlen(SOS_NFS_DIR)) {
        /* Initialise NFS */
        int err;
        printf("\nMounting NFS\n");
        if(!(err = nfs_init(&gw))){
            /* Print out the exports on this server */
            nfs_print_exports();
            if ((err = nfs_mount(SOS_NFS_DIR, &mnt_point))){
                printf("Error mounting path '%s'!\n", SOS_NFS_DIR);
            }else{
                printf("\nSuccessfully mounted '%s'\n", SOS_NFS_DIR);
            }
        }
        if(err){
            printf("Failed to initialise NFS\n");
        }
    }else{
        printf("Skipping Network initialisation since no mount point was "
             "specified\n");
    }
#endif
}

/****************************
 *** Sync library support ***
 ****************************/

/* FIXME: this needs to go - only used by
 * libnfs' rpc.c for checking if RPCs have timed out
 *
 * a much better way would be to get dev_timer to call
 * us back after 100 ms or whatever */
struct sync_ep_node {
    seL4_CPtr cap;
    //seL4_Word paddr;
    struct sync_ep_node* next;
};

struct sync_ep_node* sync_ep_list = NULL;

/* Provide an endpoint ready for use */
void * 
sync_new_ep(seL4_CPtr* ep_cap){
    // printf ("Asked for new sync EP\n");
    struct sync_ep_node *epn = sync_ep_list;
    if(epn){
        /* Use endpoint from the pool */
        sync_ep_list = epn->next;

    }else{
        /* Pool is dry... Make another endpoint */
        epn = (struct sync_ep_node*)malloc(sizeof(*epn));
        if(epn == NULL){
            printf ("%s: malloc failed\n", __FUNCTION__);
            return NULL;
        }
        epn->cap = pawpaw_create_ep_async ();
        if (!epn->cap) {
            printf ("%s: async EP creation failed\n", __FUNCTION__);
            return NULL;
        }
    }
    *ep_cap = epn->cap;
    //printf ("%s: creation succeeded @ %p\n", __FUNCTION__, epn);
    return epn;
}

/* Don't free, just recycle it to our pool of end points */
void 
sync_free_ep(void* _epn){
    // printf ("%s: freeing EP %p\n", __FUNCTION__, _epn);
    struct sync_ep_node *epn = (struct sync_ep_node*)_epn;
    assert(epn);
    /* Add to our pool */
    epn->next = sync_ep_list;
    sync_ep_list = epn;
}
