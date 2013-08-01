#include <ethdrivers/raw_iface.h>

#include "legacy_desc.h"
#include "lwip.h"
#include "io.h"
#include "enet.h"
#include "debug.h"
#include "unimplemented.h"
#include <assert.h>
#include "../../os_iface.h"

struct eth_driver imx6_eth_driver;

void ethif_print_state(struct eth_driver* driver){
    struct imx6_eth_data * eth_data = eth_driver_get_eth_data(driver);
    enet_print_state(eth_data->enet); 
    ldesc_print(eth_data->ldesc);
    enet_print_mib(eth_data->enet);
}

int
ethif_rawtx(struct eth_driver* driver, dma_addr_t buf, int length){
    struct imx6_eth_data * eth_data = eth_driver_get_eth_data(driver);
    /* create a descriptor */
    if(ldesc_txput(eth_data->ldesc, buf, length)){
        return -1;
    }else{
        enet_tx_enable(eth_data->enet);
        return 0;
    }
}


int 
ethif_rawrx(struct eth_driver* driver, rx_cb_t rxcb){
    struct imx6_eth_data* eth_data = eth_driver_get_eth_data(driver);
    dma_addr_t buf;
    int len;
    int res;

    res = ldesc_rxget(eth_data->ldesc, &buf, &len);
    
    if (res) {
        PKT_DEBUG(cprintf(COL_RX, "Receiving packet"));
        if (res == 1) {
            /* Get buffer address and size */
            uint8_t* data = (uint8_t*)os_virt(&buf);

            /* Fill the buffer and pass it to upper layers */
#ifdef CONFIG_FEC_MXC_SWAP_PACKET
            swap_packet((uint32_t *)frame->data, frame_length);
#endif

            PKT_DEBUG(printf("Receiving packet\n"));
            PKT_DEBUG(print_packet(COL_RX, data, len));
            rxcb(data, len);

#if ETH_PAD_SIZE
            pbuf_header(p, ETH_PAD_SIZE); /* reclaim the padding word */
#endif

        } else {
            FEC_DEBUG(printf("Frame error\n"));
        }

        /* Mark the currect packet as available */
        ldesc_rxput(eth_data->ldesc, buf);
        /* enable rx */
        enet_rx_enable(eth_data->enet);
        return 1;
    }else{
        return 0;
    }
}

