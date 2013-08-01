#ifndef __TIME_H
#define __TIME_H

#include <stdint.h>
#include <lwip/ip_addr.h>


/**
 * Retrieve the time using the udp time protocol
 * @param server the ip address of the time server
 * @return seconds past the UTC 1900 epoch or 0 on failure
 */
uint32_t udp_time_get(const struct ip_addr *server);



#endif /* __TIME_H */
