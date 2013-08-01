#include "raw_com.h"
#include "debug.h"
#include <ethdrivers/lwip_iface.h>
#include "unimplemented.h"

static inline struct eth_driver*
netif_get_eth_driver(struct netif* netif){
    return (struct eth_driver*)netif->state;
}


static inline struct imx6_eth_data*
netif_get_eth_data(struct netif* netif){
    return eth_driver_get_eth_data(netif_get_eth_driver(netif));
}


int ethif_irq_input(struct netif *netif);

