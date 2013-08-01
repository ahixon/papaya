#include "unimplemented.h"
#include "uboot/common.h"
#include "uboot/fec_mxc.h"
#include "io.h"
#include <ethdrivers/raw_iface.h>
#include <netif/etharp.h>
#include <lwip/stats.h>
#include "uboot/miiphy.h"
#include "uboot/micrel.h"
#include "clock.h"
#include "legacy_desc.h"
#include "debug.h"
#include "ocotp_ctrl.h"
#include <assert.h>
#include "../../os_iface.h"
#include <string.h>
#include "raw_com.h"
#include "enet.h"

#define CPU_FREQ  1000000000UL

#define DEFAULT_MAC "\x00\x19\xb8\x00\xf0\xa3"

#define MAX_PKT_SIZE    1536
#define RX_DESC_COUNT 32
#define RXBUF_SIZE  MAX_PKT_SIZE
#define TX_DESC_COUNT 128
#define TXBUF_SIZE  MAX_PKT_SIZE



void setup_iomux_enet(void);


struct eth_driver*
ethif_plat_init(int dev_id, struct ethif_os_interface interface) {
    struct enet * enet;
    struct ocotp * ocotp;
    struct clock* arm_clk;
    struct imx6_eth_data *eth_data;
    struct ldesc* ldesc;
    (void)dev_id;

    os_iface_init(interface);

    eth_data = (struct imx6_eth_data*)os_malloc(sizeof(struct imx6_eth_data));
    if (eth_data == NULL) {
        cprintf(COL_IMP, "Failed to allocate dma buffers\n");
        return NULL;
    }

    ldesc = ldesc_init(RX_DESC_COUNT, RXBUF_SIZE, TX_DESC_COUNT, TXBUF_SIZE);
    assert(ldesc);

    /* 
     * We scale up the CPU to improve benchmarking performance 
     * It is not the right place so should be moved later
     */
    arm_clk = clk_get_clock(CLK_ARM);
    clk_set_freq(arm_clk, CPU_FREQ);
    CLK_DEBUG(printf("ARM  clock frequency: %9d HZ\n", clk_get_freq(arm_clk)));

    /* initialise the eFuse controller so we can get a MAC address */
    ocotp = ocotp_init();
    /* Initialise ethernet pins */
    gpio_init();
    setup_iomux_enet();
    /* Initialise the phy library */
    miiphy_init();
    /* Initialise the phy */
    phy_micrel_init();
    /* Initialise the RGMII interface */ 
    enet = enet_init(ldesc);
    assert(enet);

    /* Fetch and set the MAC address */
    if(ocotp == NULL || ocotp_get_mac(ocotp, eth_data->ethaddr)){
        memcpy(eth_data->ethaddr, DEFAULT_MAC, 6);
    }
    enet_set_mac(enet, eth_data->ethaddr);

    /* Connect the phy to the ethernet controller */
    if(fec_init(CONFIG_FEC_MXC_PHYMASK, enet)){
        return NULL;
    }

    /* Start the controller */
    enet_enable(enet);

    /* Update book keeping */
    eth_data->irq_enabled = 0;
    eth_data->enet = enet;
    eth_data->ldesc = ldesc;
    eth_driver_set_data(&imx6_eth_driver, eth_data);
    /* done */
    return &imx6_eth_driver;
}

void __d(void){
    struct eth_driver* eth = &imx6_eth_driver;
    struct imx6_eth_data *eth_data = eth_driver_get_eth_data(eth);
    enet_print_state(eth_data->enet);
    ldesc_print(eth_data->ldesc);

}




