#ifndef __SYSCALLS_H__
#define __SYSCALLS_H__

/**
 * This is our system call endpoint cap, as defined by the root server
 */
#define SYSCALL_ENDPOINT_SLOT   (1)
#define SYSCALL_SERVICE_SLOT    (2)
#define SYSCALL_LISTEN_SLOT		(3)		// comes pre-attached for some services

/**
 * Syscall ID for writing over the network.
 */
#define SYSCALL_NETWRITE        (1)
#define SYSCALL_SBRK	        (2)
#define SYSCALL_FIND_SERVICE    (3)
#define SYSCALL_REGISTER_IRQ	(4)
#define SYSCALL_MAP_DEVICE		(5)

#define VFS_OPEN				(1)

#define VFS_SUCCESS				(0)
#define VFS_INVALID_CAP			(1)
#define VFS_FILE_NOT_FOUND		(2)

#endif