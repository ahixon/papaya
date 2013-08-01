#include <lwip/netif.h>
#include <stdint.h>

#include <ethdrivers/raw_iface.h>

/* return 0 if no packet was waiting */
int 
ethif_input(struct netif *netif);

/**
 * Should be called at the beginning of the program to set up the
 * network interface. It calls the function low_level_init() to do the
 * actual setup of the hardware.
 *
 * This function should be passed as a parameter to netif_add().
 *
 * @param netif the lwip network interface structure for this ethernetif
 * @return ERR_OK if the loopif is initialized
 *         ERR_MEM if private data couldn't be allocated
 *         any other err_t on error
 */
err_t
ethif_init(struct netif *netif);


/*
 * Handle an IRQ for this network device. This function will call 
 * ethif_input if appropriate and returns when no interrupts are pending.
 *
 * @param netif the lwip network interface structure for this ethernet
 *        interface.
 * @param irq the irq number that was triggered.
 */
void
ethif_handleIRQ(struct netif* netif, int irq);


