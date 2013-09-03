#ifndef __THREAD_H__
#define __THREAD_H__

#include <sel4/sel4.h>
#include <pid.h>
#include <vm/addrspace.h>
#include <cspace/cspace.h>

struct thread {
	char* name;
	pid_t pid;

    seL4_Word tcb_addr;
    seL4_TCB tcb_cap;

    seL4_Word ipc_buffer_addr;
    seL4_CPtr ipc_buffer_cap;

    addrspace_t as;
    cspace_t *croot;

    seL4_CPtr reply_cap;
};

typedef struct thread * thread_t;

pid_t thread_create (char* path, seL4_CPtr reply_cap, seL4_CPtr fault_ep);
void threadlist_add (pid_t pid, thread_t thread);
thread_t threadlist_lookup (pid_t pid);

#endif