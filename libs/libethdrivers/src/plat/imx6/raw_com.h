#include <ethdrivers/raw_iface.h>
struct imx6_eth_data {
    uint8_t ethaddr[6]; /* I don't think we need this */
    struct enet * enet;
    struct ldesc * ldesc;
    int irq_enabled;
};

extern struct eth_driver imx6_eth_driver;

static inline struct imx6_eth_data* 
eth_driver_get_eth_data(struct eth_driver* eth_driver){
    return (struct imx6_eth_data*)eth_driver->eth_data; 
}

static inline void
eth_driver_set_data(struct eth_driver* eth_driver, struct imx6_eth_data* data){
    eth_driver->eth_data = (struct imx6_eth_data*)data;
}
