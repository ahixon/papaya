#ifndef __THREAD_H__
#define __THREAD_H__

struct thread {
    seL4_Word tcb_addr;
    seL4_TCB tcb_cap;


    seL4_Word ipc_buffer_addr;
    seL4_CPtr ipc_buffer_cap;

    addrspace_t as;
    cspace_t *croot;
};

typedef struct thread * thread_t;

#endif