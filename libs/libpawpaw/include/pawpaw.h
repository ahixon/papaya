#ifndef __PAWPAW_H__
#define __PAWPAW_H__

#include <sel4/sel4.h>

#define PAPAYA_SYSCALL_SLOT			(1)
#define PAPAYA_TCB_SLOT				(2)
#define PAPAYA_ROOT_CNODE_SLOT		(3)
#define PAPAYA_PAGEDIR_SLOT			(4)
#define PAPAYA_INITIAL_FREE_SLOT	(5)

#define PAPAYA_IPC_PAGE				0xA0001000
#define PAPAYA_IPC_PAGE_SIZE		0x1000

#define PAPAYA_CSPACE_DEPTH			32		/* ensure this stays up to date with libsel4cspace! */
#define PAPAYA_BEAN_SIZE			(1 << seL4_PageBits)

typedef struct sbuf* sbuf_t;

/* FIXME: in the future, maybe register a device struct? */
seL4_CPtr pawpaw_register_irq (int irq_num);

seL4_CPtr pawpaw_save_reply (void);

seL4_CPtr pawpaw_cspace_alloc_slot (void);
void pawpaw_cspace_free_slot (seL4_CPtr);

void* pawpaw_map_device (unsigned int base, unsigned int size);

seL4_CPtr pawpaw_service_lookup (char* name);
//seL4_CPtr pawpaw_service_lookup_nb (char* name);

seL4_CPtr pawpaw_create_ep_async (void);
seL4_CPtr pawpaw_create_ep (void);

int pawpaw_bind_async_to_thread (seL4_CPtr async_ep);

void pawpaw_suicide (void);

int pawpaw_register_service (seL4_CPtr ep);

//struct pawpaw_can* pawpaw_can_negotiate (seL4_CPtr other, unsigned int max_beans);

sbuf_t pawpaw_sbuf_create (unsigned int size);
sbuf_t pawpaw_sbuf_mount (seL4_CPtr cap);

int pawpaw_sbuf_install (sbuf_t sb);
sbuf_t pawpaw_sbuf_fetch (seL4_Word idx);

void* pawpaw_sbuf_slot_get (sbuf_t sb, unsigned int idx);
int pawpaw_sbuf_slot_next (sbuf_t sb);

/* FIXME: just make the struct public? */
seL4_CPtr pawpaw_sbuf_get_cap (sbuf_t sb);
seL4_Word pawpaw_sbuf_get_id (sbuf_t sb);

#if 0
struct pawpaw_can* pawpaw_can_set (seL4_Word id, struct pawpaw_can* can);
struct pawpaw_can* pawpaw_can_fetch (seL4_Word id);

void* pawpaw_map_in_shared (seL4_CPtr cap);
int pawpaw_map_out_shared (void);
#endif
#endif