/*
 * Initialises the network stack and NFS library.
 * Originally by Ben Leslie (2003)
 * Modified by Alex Hixon
 */

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

#define ARP_PRIME_TIMEOUT_MS     1000
#define ARP_PRIME_RETRY_DELAY_MS   10

static struct net_irq {
    int irq;
    seL4_IRQHandler cap;
} *_net_irqs = NULL;
static int _nirqs = 0;

static seL4_CPtr _irq_ep;

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

static void network_prime_arp (struct ip_addr *gw) {
    int timeout = ARP_PRIME_TIMEOUT_MS;
    struct eth_addr* eth;
    struct ip_addr* ip;

    while (timeout > 0) {
        /* Send an ARP request */
        etharp_request(_netif, gw);

        /* Wait for the response */
        sos_usleep (ARP_PRIME_RETRY_DELAY_MS * 1000);
        if (etharp_find_addr (_netif, gw, &eth, &ip) == -1) {
            timeout += ARP_PRIME_RETRY_DELAY_MS;
        } else {
            return;
        }
    }
}

int network_init (seL4_CPtr interrupt_ep) {
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
    if (err) {
        return false;
    }

    //conditional_panic(err, "Failed to parse IP address configuration");
    //printf("  Local IP Address: %s\n", ipaddr_ntoa( &ipaddr));
    //printf("Gateway IP Address: %s\n", ipaddr_ntoa(     &gw));
    //printf("      Network Mask: %s\n", ipaddr_ntoa(&netmask));
    //printf("\n");

    /* low level initialisation */
    eth_driver = ethif_plat_init (0, sos_interface);
    if (!eth_driver) {
        return false;
    }

    /* Initialise IRQS */
    irqs = ethif_enableIRQ (eth_driver, &_nirqs);
    _net_irqs = (struct net_irq*)calloc (_nirqs, sizeof (*_net_irqs));
    for (i = 0; i < _nirqs; i++){
        _net_irqs[i].irq = irqs[i];
        _net_irqs[i].cap = enable_irq (irqs[i], _irq_ep);
    }

    /* Setup the network interface */
    lwip_init ();
    _netif = (struct netif*)malloc (sizeof (*_netif));
    if (!_netif) {
        return false;
    }
    
    _netif = netif_add (_netif, &ipaddr, &netmask, &gw, 
                        eth_driver, ethif_init, ethernet_input);
    if (!_netif) {
        return false;
    }
    
    netif_set_up (_netif);
    netif_set_default (_netif);

    /*
     * LWIP does not queue packets while waiting for an ARP response 
     * Generally this is okay as we block waiting for a response to our
     * request before sending another. On the other hand, priming the
     * table is cheap and can save a lot of heart ache 
     */
    network_prime_arp (&gw);
    
    return true;
}