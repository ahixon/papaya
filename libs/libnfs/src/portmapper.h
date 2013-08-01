#ifndef __PORTMAPPER_H
#define __PORTMAPPER_H

#include <stdint.h>
#include <lwip/ip_addr.h>

/**
 * Create a new UDP connection using port mapper
 * @param[in] server the IP address of the server to contact
 * @param[in] prog the program number
 * @param[in] vers the version number
 * @return     the port number that the server has opened. 
 *           0 indicates
 *          -1 indicated an RPC error
 *          -2 indicated a server error
 */
int portmapper_getport(const ip_addr_t *server, uint32_t prog, uint32_t vers);

#endif /* __PORTMAPPER_H */
