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

seL4_MessageInfo_t syscall_service_find (thread_t thread);
seL4_MessageInfo_t syscall_service_register (thread_t thread);
seL4_MessageInfo_t syscall_register_irq (thread_t thread);
seL4_MessageInfo_t syscall_map_device (thread_t thread);

seL4_MessageInfo_t syscall_alloc_cnodes (thread_t thread);
seL4_MessageInfo_t syscall_create_ep_sync (thread_t thread);
seL4_MessageInfo_t syscall_create_ep_async (thread_t thread);

seL4_MessageInfo_t syscall_bind_async_tcb (thread_t thread);
seL4_MessageInfo_t syscall_suicide (thread_t thread);

seL4_MessageInfo_t syscall_sbuf_create (thread_t thread);
seL4_MessageInfo_t syscall_sbuf_mount (thread_t thread);

#endif