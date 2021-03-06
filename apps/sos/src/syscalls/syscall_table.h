#ifndef __SYSCALL_TABLE_H__
#define __SYSCALL_TABLE_H__

#include <thread.h>
#include <pawpaw.h>

typedef seL4_MessageInfo_t (*syscall)(thread_t thread);

struct syscall_info {
    syscall scall_func;
    unsigned int argcount;
    short reply;
};

/* FIXME: move these and the .c code into just a single .h ? */
int syscall_sbrk (struct pawpaw_event* evt);

int syscall_thread_suicide (struct pawpaw_event* evt);
int syscall_thread_create (struct pawpaw_event* evt);
int syscall_thread_destroy (struct pawpaw_event* evt);
int syscall_thread_pid (struct pawpaw_event* evt);
int syscall_thread_wait (struct pawpaw_event* evt);
int syscall_thread_list (struct pawpaw_event* evt);

int syscall_alloc_cnodes (struct pawpaw_event* evt);
int syscall_create_ep_sync (struct pawpaw_event* evt);
int syscall_create_ep_async (struct pawpaw_event* evt);
int syscall_bind_async_tcb (struct pawpaw_event* evt);

int syscall_register_irq (struct pawpaw_event* evt);
int syscall_map_device (struct pawpaw_event* evt);

int syscall_service_find (struct pawpaw_event* evt);
int syscall_service_register (struct pawpaw_event* evt);

int syscall_share_create (struct pawpaw_event* evt);
int syscall_share_mount (struct pawpaw_event* evt);
int syscall_share_unmount (struct pawpaw_event* evt);

int syscall_alloc_dma (struct pawpaw_event* evt);

seL4_CPtr service_lookup (char* name);
struct as_region* create_share_reg (seL4_CPtr *cap, seL4_Word *dest_id, int map);

void finish_thread_create (struct pawpaw_event *evt, struct frameinfo* frame);
#endif