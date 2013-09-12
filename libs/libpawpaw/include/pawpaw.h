#ifndef __PAWPAW_H__
#define __PAWPAW_H__

#include <sel4/sel4.h>

#define PAPAYA_SYSCALL_SLOT			(1)
#define PAPAYA_TCB_SLOT				(2)
#define PAPAYA_ROOT_CNODE_SLOT		(3)
#define PAPAYA_PAGEDIR_SLOT			(4)
#define PAPAYA_INITIAL_FREE_SLOT	(5)

#define SYSCALL_SBRK	        	(2)
#define SYSCALL_FIND_SERVICE   		(3)
#define SYSCALL_REGISTER_IRQ		(4)
#define SYSCALL_MAP_DEVICE			(5)
#define SYSCALL_ALLOC_CNODES		(6)
#define SYSCALL_CREATE_EP_SYNC		(7)
#define SYSCALL_CREATE_EP_ASYNC		(8)
#define SYSCALL_BIND_AEP_TCB		(9)
#define SYSCALL_SUICIDE				(10)
#define SYSCALL_REGISTER_SERVICE	(11)

#define PAPAYA_CSPACE_DEPTH			32		/* ensure this stays up to date with libsel4cspace! */

/* FIXME: in the future, maybe register a device struct? */
seL4_CPtr pawpaw_register_irq (int irq_num);

seL4_CPtr pawpaw_save_reply (void);

seL4_CPtr pawpaw_cspace_alloc_slot (void);
void pawpaw_cspace_free_slot (seL4_CPtr);

void* pawpaw_map_device (unsigned int base, unsigned int size);

seL4_CPtr pawpaw_service_lookup (char* name);

seL4_CPtr pawpaw_create_ep_async (void);
seL4_CPtr pawpaw_create_ep (void);

int pawpaw_bind_async_to_thread (seL4_CPtr async_ep);

void pawpaw_suicide (void);

int pawpaw_register_service (seL4_CPtr ep);
#endif