#ifndef __SYSCALLS_H__
#define __SYSCALLS_H__

/**
 * This is our system call endpoint cap, as defined by the root server
 */
#define SYSCALL_ENDPOINT_SLOT   (1)

/**
 * Syscall ID for writing over the network.
 */
#define SYSCALL_NETWRITE        (1)

#define SYSCALL_SBRK	        (2)

#endif