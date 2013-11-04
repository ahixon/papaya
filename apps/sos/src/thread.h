#ifndef __THREAD_H__
#define __THREAD_H__

#include <sel4/sel4.h>
#include <uid.h>
#include <vm/addrspace.h>
#include <cspace/cspace.h>
#include <pawpaw.h>

typedef struct thread * thread_t;

#if 0
struct req_svc {
    //char* svc;
    seL4_CPtr cap;
    struct thread* svc_thread;

    struct req_svc* next;
};
#endif

struct thread_resource {
    seL4_Word addr;
    int size;
    struct thread_resource *next;
};

struct thread {
	char* name;
	pid_t pid;
    uint64_t start;

    seL4_Word tcb_addr;
    seL4_TCB tcb_cap;

    addrspace_t as;
    cspace_t *croot;
    struct thread_resource* resources;

    seL4_CPtr service_cap;
    struct pawpaw_saved_event* bequests;
    int default_caps;

    char* static_stack;

    thread_t next;
};

thread_t thread_create_from_fs (char* path, seL4_CPtr rootsvr_ep);
thread_t thread_create_from_cpio (char* path, seL4_CPtr rootsvr_ep);
thread_t thread_create_internal (char* name, void* initial_pc, unsigned int stack_size);


void thread_destroy (thread_t thread);

void threadlist_add (pid_t pid, thread_t thread);
thread_t thread_lookup (pid_t pid);

thread_t threadlist_first (void);

int thread_cspace_new_cnodes (thread_t t, int num, seL4_CPtr* cnode);

seL4_CPtr thread_cspace_new_ep (thread_t thread);
seL4_CPtr thread_cspace_new_async_ep (thread_t thread);

#endif