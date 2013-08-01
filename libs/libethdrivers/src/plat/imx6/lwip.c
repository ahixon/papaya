#include "unimplemented.h"
#include "uboot/common.h"
#include "uboot/fec_mxc.h"
#include "io.h"
#include <ethdrivers/lwip_iface.h>
#include <netif/etharp.h>
#include <lwip/stats.h>
#include "uboot/miiphy.h"
#include "uboot/micrel.h"
#include "uboot/fec_mxc.h"
#include <string.h>
#include "lwip.h"
#include "legacy_desc.h"
#include <stdlib.h>
#include "../../os_iface.h"
#include "enet.h"

#include <assert.h>

#define FEC_XFER_TIMEOUT	5000

extern struct ethif_os_interface global_interface;

void imx6_eth_get_mac(struct netif* netif, uint8_t* hwaddr){
    enet_get_mac(netif_get_eth_data(netif)->enet, hwaddr);
}

static struct pbuf *
imx6_low_level_input(struct netif *netif){
    struct imx6_eth_data* eth_data = netif_get_eth_data(netif);
    struct enet* enet = eth_data->enet;
    struct pbuf *p, *q;
    dma_addr_t buf;
    int len;
    int res;
    p = NULL;

    res = ldesc_rxget(eth_data->ldesc, &buf, &len);
    if (res) {
        PKT_DEBUG(printf("Receiving packet"));
        if (res == 1) {
            uint8_t* data = (uint8_t*)os_virt(&buf);

            /* Fill the buffer and pass it to upper layers */
#ifdef CONFIG_FEC_MXC_SWAP_PACKET
            swap_packet((uint32_t *)data, frame_length);
#endif
#if ETH_PAD_SIZE
            len += ETH_PAD_SIZE; /* allow room for Ethernet padding */
#endif
            /* We allocate a pbuf chain of pbufs from the pool. */
            p = pbuf_alloc(PBUF_RAW, len, PBUF_POOL);
            if (p == NULL) {
                PKT_DEBUG(printf("OOM for pbuf: packet dropped\n"));
            }else{
                uint8_t* frame_pos = (uint8_t*)data;
#if ETH_PAD_SIZE
                pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif
                /* fill the pbuf chain */
                for(q = p; q != NULL; q = q->next) {
                    memcpy(q->payload, frame_pos, q->len);
                    frame_pos += q->len;
                }
                PKT_DEBUG(print_packet(COL_RX, data, len));
            }

#if ETH_PAD_SIZE
            pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif
            LINK_STATS_INC(link.recv);

        } else {
            PKT_DEBUG(printf("Packet error\n"));
            LINK_STATS_INC(link.memerr);
            LINK_STATS_INC(link.drop);
        }

        /* Mark the currect packet as available */
        ldesc_rxput(eth_data->ldesc, buf);
        enet_rx_enable(enet);
    }
    return p;
}


static err_t
imx6_low_level_output(struct netif *netif, struct pbuf *p){
    struct imx6_eth_data * eth_data = netif_get_eth_data(netif);
    dma_addr_t buf;
    int len;
    char* pkt_pos;
    struct pbuf *q;

    err_t ret;

#if ETH_PAD_SIZE
    pbuf_header(p, -ETH_PAD_SIZE); /* drop the padding word */
#endif

    len = ldesc_txget(eth_data->ldesc, &buf);
    if(len == 0){
        BUF_DEBUG(printf("No TX descriptors available\n"));
#if 1
        /* Wait for a descriptor to be available */
        while(len == 0){
            len = ldesc_txget(eth_data->ldesc, &buf);
        }
#else
        /* Return an error */
        return -1;
#endif
        BUF_DEBUG(printf("Got TX descriptor... Continuing\n"));
    }

    if ((p->tot_len > len) || (p->tot_len <= 0)) {
        printf("Payload (%d) too large\n", p->tot_len);
        return -1;
    }
    pkt_pos = (char*)os_virt(&buf);
    for(q = p; q != NULL; q = q->next) {
        memcpy(pkt_pos, q->payload, q->len);
        pkt_pos += q->len;
    }

    PKT_DEBUG(cprintf(COL_TX, "Sending packet"));
    PKT_DEBUG(print_packet(COL_TX, (void*)os_virt(&buf), p->tot_len));

    ret = ethif_rawtx(netif_get_eth_driver(netif), buf, p->tot_len);

#if ETH_PAD_SIZE
    pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif
  
    LINK_STATS_INC(link.xmit);

    return ret;
}

int ethif_input(struct netif *netif){
    struct imx6_eth_data* eth_data = netif_get_eth_data(netif);
    uint32_t txe;

    /* 
     * This is a good time to make sure that we have no buffers waiting to be 
     * transmitted. The problem occurs because there is a window during TX 
     * shutdown in which enabling the transmitter has no effect. Normally an
     * IRQ event would tell us to restart the TX logic but if this function is
     * being called, it is assumed that IRQs are not enabled.
     */
    txe = enet_clr_events(eth_data->enet, NETIRQ_TXF);
    if(txe && !ldesc_txcomplete(eth_data->ldesc)){
        enet_tx_enable(eth_data->enet);
    }
    /* Fake an RX IRQ event */
    return ethif_irq_input(netif);
}


int
ethif_irq_input(struct netif *netif){
    struct eth_hdr *ethhdr;
    struct pbuf *p;

    /* move received packet into a new pbuf */
    p = imx6_low_level_input(netif);

    /* no packet could be read, silently ignore this */
    if (p == NULL) {
        return 0;
    }

    /* points to packet payload, which starts with an Ethernet header */
    ethhdr = p->payload;

    switch (htons(ethhdr->type)) {
    /* IP or ARP packet? */
    case ETHTYPE_IP:
    case ETHTYPE_ARP:
#if PPPOE_SUPPORT
    /* PPPoE packet? */
    case ETHTYPE_PPPOEDISC:
    case ETHTYPE_PPPOE:
#endif /* PPPOE_SUPPORT */
    /* full packet send to tcpip_thread to process */
        if (netif->input(p, netif)!=ERR_OK) { 
            LWIP_DEBUGF(NETIF_DEBUG, ("ethernetif_input: IP input error\n"));
            pbuf_free(p);
            p = NULL;
        }
    break;

    default:
        pbuf_free(p);
        p = NULL;
    break;
    }
    return 1;
}

static void
imx6_low_level_init(struct netif *netif){

    netif->hwaddr_len = ETHARP_HWADDR_LEN;
    imx6_eth_get_mac(netif, netif->hwaddr);
    /* maximum transfer unit */
    netif->mtu = MAX_PKT_SIZE;
  
    /* device capabilities */
    /* don't set NETIF_FLAG_ETHARP if this device is not an ethernet one */
    netif->flags =  NETIF_FLAG_BROADCAST | 
                    NETIF_FLAG_ETHARP | 
                    NETIF_FLAG_LINK_UP;

}

err_t
ethif_init(struct netif *netif){
    if(netif->state == NULL){
        return ERR_ARG;
    }

#if LWIP_NETIF_HOSTNAME
    /* Initialize interface hostname */
    netif->hostname = "imx6_sabrelite";
#endif /* LWIP_NETIF_HOSTNAME */

    NETIF_INIT_SNMP(netif, snmp_ifType_ethernet_csmacd, LINK_SPEED_OF_YOUR_NETIF_IN_BPS);

    netif->name[0] = 'K';
    netif->name[1] = 'Z';

    netif->output = etharp_output;
    netif->linkoutput = imx6_low_level_output;

    imx6_low_level_init(netif);

    return ERR_OK;
}




