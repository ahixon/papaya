#ifndef __ETHIFACE_RAW_IFACE_H__
#define __ETHIFACE_RAW_IFACE_H__

#include <stdint.h>
#include <stdlib.h>

struct eth_driver {
    void* eth_data;
};

typedef void * eth_paddr_t;
typedef void * eth_vaddr_t;

typedef struct dma_addr {
    eth_paddr_t phys;
    eth_vaddr_t virt;
}dma_addr_t;

struct ethif_os_interface {
    void *cookie;
    void (*clean)(void *cookie, eth_vaddr_t addr, int range);
    void (*invalidate)(void *cookie, eth_vaddr_t addr, int range);
    dma_addr_t (*dma_malloc)(void *cookie, size_t size, int cached);
    void* (*malloc)(void *cookie, size_t size);
    void* (*ioremap)(void *cookie, eth_paddr_t dev_paddr, int size);
};

/* 
 * This will be called when a packet arrives 
 * @param packet the physical address of the received packet
 * @param len the length of the received packet
 */
typedef int (*rx_cb_t)(eth_paddr_t buf, int len);

/**
 * This function initialises the hardware
 * @param[in] dev_id    The device id of the driver to initialise.
 *                      Ignored on platforms that have only 1 device
 * @param[in] interface A structure containing os specific data and
 *                      functions.
 * @return              A reference to the ethernet drivers state.
 */
struct eth_driver* ethif_plat_init(int dev_id, struct ethif_os_interface interface);

/* 
 * The following functions can be used to send and receive
 * raw packets to/from the phy
 */
int ethif_rawtx(struct eth_driver* driver, dma_addr_t buf, int length);

/* returns 0 if no packet was waiting */
int ethif_rawrx(struct eth_driver* driver, rx_cb_t rxcb);


/*
 * Enables irqs but does not provide a handling thread. Returns an array of 
 * IRQs managed by this driver.
 * @param[in]  eth_driver  The ethernet driver for which IRQs should be enabled.
 * @param[out]      nirqs  On return, "nirqs" will be filled with the number
 *                         of IRQs managed by this device.
 * @return                 An array of "nirqs" elements that each represent an
 *                         IRQ number. 
 */
const int* ethif_enableIRQ(struct eth_driver* driver, int* nirqs);

void ethif_raw_handleIRQ(struct eth_driver* driver, int irq, rx_cb_t rxcb);


/* Debug methods */

void ethif_print_state(struct eth_driver* driver);

#endif /* __ETHIFACE_RAW_IFACE_H__ */

