#include <lwip/netif.h>

/* 
 * Start a UDP echo server and return 
 */
int udpecho_server(struct netif* netif);


/* 
 * Start a UDP echo client and return 
 */
int udpecho_client(struct netif* netif, ip_addr_t *addr, void (*sleep)(int us));

