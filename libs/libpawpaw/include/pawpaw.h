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

#define PAWPAW_EVENT_UNHANDLED		(0)
#define PAWPAW_EVENT_HANDLED		(1)
#define PAWPAW_EVENT_NEEDS_REPLY	(2)
#define PAWPAW_EVENT_HANDLED_SAVED	(3)

#define PAWPAW_EVENT_INVALID		(-2)

#define HANDLER_NONE				0
#define HANDLER_REPLY				1
#define HANDLER_AUTOMOUNT			2

#define PAPAYA_CSPACE_DEPTH			32		/* ensure this stays up to date with libsel4cspace! */

struct pawpaw_event {
	seL4_Word badge;
	seL4_MessageInfo_t msg;
	seL4_CPtr reply_cap;
	int flags;
	seL4_MessageInfo_t reply;
	seL4_Word *args;
	struct pawpaw_share* share;
};

struct pawpaw_saved_event {
	struct pawpaw_event* evt;
	struct pawpaw_saved_event* next;
};

struct pawpaw_eventhandler_info {
    int (*func)(struct pawpaw_event* evt);
    unsigned int argcount;
    short flags;
};

struct pawpaw_event_table {
	unsigned int num_events;
	struct pawpaw_eventhandler_info* handlers;	/* should be an array, don't want to be stuck to C99 */
};

struct pawpaw_share {
	seL4_Word id;
	seL4_CPtr cap;
	void* buf;
	int loaded;
	int sent;
};

/* FIXME: in the future, maybe register a device struct? */
seL4_CPtr pawpaw_register_irq (int irq_num);
void* pawpaw_map_device (unsigned int base, unsigned int size);
seL4_Word pawpaw_dma_alloc (void *vaddr, unsigned int sizebits);

seL4_CPtr pawpaw_save_reply (void);

seL4_CPtr pawpaw_cspace_alloc_slot (void);
void pawpaw_cspace_free_slot (seL4_CPtr);

seL4_CPtr pawpaw_service_lookup (char* name);
//seL4_CPtr pawpaw_service_lookup_nb (char* name);
int pawpaw_register_service (seL4_CPtr ep);

seL4_CPtr pawpaw_create_ep_async (void);
seL4_CPtr pawpaw_create_ep (void);

int pawpaw_bind_async_to_thread (seL4_CPtr async_ep);

void pawpaw_suicide (void);

void pawpaw_event_init (void);
void pawpaw_event_loop (struct pawpaw_event_table* table, void (*interrupt_func)(struct pawpaw_event* evt), seL4_CPtr ep);
struct pawpaw_event* pawpaw_event_create (seL4_MessageInfo_t msg, seL4_Word badge);
void pawpaw_event_dispose (struct pawpaw_event* evt);
int pawpaw_event_process (struct pawpaw_event_table* table, struct pawpaw_event *evt, seL4_CPtr (*save_reply_func)(void));
seL4_CPtr pawpaw_event_get_recv_cap (void);

struct pawpaw_share* pawpaw_share_new (void);
struct pawpaw_share* pawpaw_share_mount (seL4_CPtr cap);
int pawpaw_share_unmount (struct pawpaw_share* share);
int pawpaw_share_attach (struct pawpaw_share* share);

struct pawpaw_share* pawpaw_share_get (seL4_Word id);
void pawpaw_share_set (struct pawpaw_share* share);

#endif