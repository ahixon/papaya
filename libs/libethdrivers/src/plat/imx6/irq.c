
#include "lwip.h"
#include <ethdrivers/raw_iface.h>
#include "uboot/fec_mxc.h"
#include "legacy_desc.h"
#include "enet.h"
#include <assert.h>

#define ENETINT_TSAVAIL  (1<<16)
#define ENETINT_WAKEUP   (1<<17)
#define ENETINT_PLR      (1<<18)
#define ENETINT_UN       (1<<19)
#define ENETINT_RL       (1<<20)
#define ENETINT_LC       (1<<21)
#define ENETINT_EBERR    (1<<22)
#define ENETINT_MII      (1<<23)
#define ENETINT_RXB      (1<<24) /* descriptor updated */
#define ENETINT_RXF      (1<<25) /* frame complete     */
#define ENETINT_TXB      (1<<26) /* descriptor updated */
#define ENETINT_TXF      (1<<27) /* frame complete     */
#define ENETINT_GRA      (1<<28)
#define ENETINT_BABT     (1<<29)
#define ENETINT_BABR     (1<<30)

#include <ethdrivers/lwip_iface.h>

#define INTERRUPT_ENET          150
#define INTERRUPT_ENET_TIMER    151
static const int net_irqs[] = {
        INTERRUPT_ENET,
        INTERRUPT_ENET_TIMER,
        0
    };




const int* ethif_enableIRQ(struct eth_driver* driver, int *nirqs){
    struct imx6_eth_data *eth_data = eth_driver_get_eth_data(driver);
    struct enet* enet = eth_data->enet; 
    int i;
    enet_enable_events(enet, 0);
    enet_clr_events(enet, ~(NETIRQ_RXF | NETIRQ_TXF | NETIRQ_EBERR));
    enet_enable_events(enet, NETIRQ_RXF | NETIRQ_TXF | NETIRQ_EBERR);
    eth_data->irq_enabled = 1;
    /* Count the IRQS. This is complex for backwards compatibility 
     * The IRQ list may be NULL terminated, else we can just take the size.
     */
    for(i = 0; i < sizeof(net_irqs)/sizeof(*net_irqs) && net_irqs[i] != 0; i++);
    *nirqs = i;
    return net_irqs;
}

void 
ethif_raw_handleIRQ(struct eth_driver* eth_driver, int irq, rx_cb_t rxcb){
    struct imx6_eth_data *eth_data = eth_driver_get_eth_data(eth_driver);
    struct enet* enet = eth_data->enet; 
    uint32_t e;
    while((e = enet_clr_events(enet, NETIRQ_RXF | NETIRQ_TXF | NETIRQ_EBERR))){
        if(e & NETIRQ_TXF){
            int enabled, complete;
            e &= ~NETIRQ_TXF;
            enabled = enet_tx_enabled(enet);
            complete = ldesc_txcomplete(eth_data->ldesc);
            if(!enabled && !complete){
                enet_tx_enable(enet);
            }
        }
        if(e & ENETINT_RXF){
            ethif_rawrx(eth_driver, rxcb);
        }
        if(e & NETIRQ_EBERR){
            printf("Error: System bus/uDMA\n");
            ethif_print_state(eth_driver);
            assert(0);
            while(1);
        }
 
        if(e){
            printf("Unhandled irqs 0x%x\n", e);
        }
    }
}


void 
ethif_handleIRQ(struct netif* netif, int irq){
    struct imx6_eth_data *eth_data = netif_get_eth_data(netif);
    struct enet* enet = eth_data->enet; 
    uint32_t e;
    while((e = enet_clr_events(enet, NETIRQ_RXF | NETIRQ_TXF | NETIRQ_EBERR))){
        if(e & NETIRQ_TXF){
            int enabled, complete;
            e &= ~NETIRQ_TXF;
            enabled = enet_tx_enabled(enet);
            complete = ldesc_txcomplete(eth_data->ldesc);
            if(!enabled && !complete){
                enet_tx_enable(enet);
            }
        }
        if(e & NETIRQ_RXF){
            e &= ~NETIRQ_RXF;
            while(ethif_irq_input(netif));
        }
        if(e & NETIRQ_EBERR){
            printf("Error: System bus/uDMA\n");
            ethif_print_state(netif_get_eth_driver(netif));
            assert(0);
            while(1);
        }
        if(e){
            printf("Unhandled irqs 0x%x\n", e);
        }
    }
}


