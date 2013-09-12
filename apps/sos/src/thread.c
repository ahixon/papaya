#include <stdlib.h>
#include <string.h>

#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <pawpaw.h>

#include <vm/vm.h>
#include <vm/addrspace.h>

#include "ut_manager/ut.h"

#include <elf/elf.h>
#include "elf.h"

#include "vm/vmem_layout.h"
#include <cpio/cpio.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>
#include <assert.h>

#define DEFAULT_PRIORITY		(0)

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];

thread_t threadlist[PID_MAX] = {0};

thread_t running_head = NULL;
thread_t running_tail = NULL;

seL4_CPtr thread_cspace_new_ep (thread_t thread) {
    int err;
    seL4_CPtr service_cap;

    seL4_Word service_addr = ut_alloc (seL4_EndpointBits);
    if (!service_addr) {
        return 0;
    }

    err = cspace_ut_retype_addr (service_addr,
                                 seL4_EndpointObject, seL4_EndpointBits,
                                 thread->croot, &service_cap);
    if (err) {
        return 0;
    }

    return service_cap;
}

seL4_CPtr thread_cspace_new_async_ep (thread_t thread) {
    int err;
    seL4_CPtr async_ep;

    seL4_Word aep_addr = ut_alloc (seL4_EndpointBits);
    if (!aep_addr) {
        return 0;
    }

    err = cspace_ut_retype_addr (aep_addr,
                                 seL4_AsyncEndpointObject, seL4_EndpointBits,
                                 thread->croot, &async_ep);
    if (err) {
        return 0;
    }

    return async_ep;
}

/*
 * Attempts to allocate n contiguous spots in the thread's CSpace.
 * 
 * Returns the number of successful CNodes allocated. If the number was
 * less than the requested amount, but non-zero, then the caller is free to
 * call this function again with the remaning allocation (although they will
 * not be contiguous with the previous allocation).
 * 
 * If zero CNodes are returned, then the caller should NOT try again, unless
 * more space is made in the CSpace (usually be deleting other CNodes).
 */
int thread_cspace_new_cnodes (thread_t thread, int num, seL4_CPtr* cnode) {
    seL4_CPtr cap = cspace_alloc_slot (thread->croot);
    if (cap == CSPACE_NULL) {
        return 0;
    }

    seL4_CPtr prev_cap = cap;
    int alloc = 1;

    while (alloc < num) {
        seL4_CPtr cur_cap = cspace_alloc_slot (thread->croot);
        if (cur_cap == CSPACE_NULL) {
            /* no more room left */
            break;
        }

        if (cur_cap - 1 != prev_cap) {
            /* no longer contiguous, free just-allocated slot and return */
            cspace_free_slot (thread->croot, cur_cap);
            break;
        }

        alloc++;
    }

    *cnode = cap;
    return alloc;
}


/* FIXME: error codes would be nicer */
pid_t thread_create (char* path, seL4_CPtr reply_cap) {
	int err;
    seL4_CPtr last_cap, service_cap = 0;
    seL4_UserContext context;

    char* elf_base;
    unsigned long elf_size;

    /* Try to allocate a process ID */
    pid_t pid = pid_next();
    if (pid < 0) {
        /* no more process IDs left */
    	return -1;
    }

    /* Create housekeeping info */
	thread_t thread = malloc (sizeof (struct thread));
	if (!thread) {
		return -1;
	}

    thread->next = NULL;

	/* FIXME: actually check that path terminates! */
	thread->name = strdup (path);

    /* Create a new TCB object */
    thread->tcb_addr = ut_alloc (seL4_TCBBits);
    if (!thread->tcb_addr) {
    	goto cleanupTCB;
    }

    if (cspace_ut_retype_addr (thread->tcb_addr,
                               seL4_TCBObject, seL4_TCBBits,
                               cur_cspace, &thread->tcb_cap)) {
    	goto cleanupTCB;
    }

    /* create address space for process */
    thread->as = addrspace_create (0);
    if (!thread->as) {
    	goto cleanupAS;
    }

    /* Map in IPC first off (since we need it for TCB configuration) */
    as_define_region (thread->as, PROCESS_IPC_BUFFER, PAGE_SIZE, seL4_AllRights, REGION_IPC);
    if (!as_map_page (thread->as, PROCESS_IPC_BUFFER)) {
        goto cleanupAS;
    }

    seL4_CPtr ipc_cap = as_get_page_cap (thread->as, PROCESS_IPC_BUFFER);
    if (!ipc_cap) {
    	goto cleanupAS;
    }

    /* Create thread's CSpace (which we will manage in-kernel - although they
     * get to manage any empty CNodes they request.
     *
     * Apparently, we must have a level 2 CSpace otherwise the thread can't
     * store caps it receives from other threads via IPC. Bug in seL4? or the
     * way libsel4cspace creates the CSpace?
     */
    thread->croot = cspace_create (2);
    if (!thread->croot) {
        goto cleanupCSpace;
    }

    /* Copy a whole bunch of default caps to their cspace, namely:
     *  - a minted reply cap (with their PID), so that they can do IPC to the
     *    root server
     *  - their TCB cap
     *  - their root CNode cap
     *  - their pagedir cap
     * 
     *  (in that order)
     */

    last_cap = cspace_mint_cap(thread->croot, cur_cspace, reply_cap, seL4_AllRights, seL4_CapData_Badge_new (pid));
    assert (last_cap == PAPAYA_SYSCALL_SLOT);

    last_cap = cspace_copy_cap (thread->croot, cur_cspace, thread->tcb_cap, seL4_AllRights);
    assert (last_cap == PAPAYA_TCB_SLOT);

    last_cap = cspace_copy_cap (thread->croot, cur_cspace, thread->croot->root_cnode, seL4_AllRights);
    assert (last_cap == PAPAYA_ROOT_CNODE_SLOT);

    last_cap = cspace_copy_cap (thread->croot, cur_cspace, thread->as->pagedir_cap, seL4_AllRights);
    assert (last_cap == PAPAYA_PAGEDIR_SLOT);
    
    /* Now, allocate an initial free slot */
    last_cap = cspace_alloc_slot (thread->croot);
    assert (last_cap == PAPAYA_INITIAL_FREE_SLOT);

    /* The cap that we should return if someone asks for a service */
    thread->service_cap = NULL;

    /* and do a mbox */
    /* XXX: map in mbox area */
    if (!as_define_region (thread->as, PROCESS_IPC_BUFFER + PAGE_SIZE, PAGE_SIZE * 3, seL4_AllRights, REGION_GENERIC)) {
        printf ("failed to define mbox\n");
        return 0;
    }

    vaddr_t mbox_addr = PROCESS_IPC_BUFFER + (2*PAGE_SIZE);
    as_map_page (thread->as, mbox_addr);
    seL4_CPtr mbox_copy = cspace_copy_cap (thread->croot, cur_cspace, as_get_page_cap (thread->as, mbox_addr), seL4_AllRights);
    printf ("and mbox copy is %d\n", mbox_copy);


    /* Configure the TCB */
    err = seL4_TCB_Configure(thread->tcb_cap, PAPAYA_SYSCALL_SLOT, DEFAULT_PRIORITY,
                             thread->croot->root_cnode, seL4_NilData,
                             thread->as->pagedir_cap, seL4_NilData, PROCESS_IPC_BUFFER,
                             ipc_cap);
    if (err) {
    	goto cleanupAS;
    }

    /* parse the dite image */
    /* FIXME: look up using read */
    dprintf(1, "\nStarting \"%s\"...\n", path);
    elf_base = cpio_get_file(_cpio_archive, path, &elf_size);
    if (!elf_base) {
    	goto cleanupAS;
    }

    /* load the elf image */
    if (elf_load(thread->as, elf_base)) {
    	goto cleanupAS;
    }

    /* find where we put the stack */
    struct as_region* stack = as_get_region_by_type (thread->as, REGION_STACK);
    vaddr_t stack_top = stack->vbase + stack->size;

    printf ("Thread's address space looks like:\n");
    addrspace_print_regions (thread->as);

    /* install into threadlist before we start */
    threadlist_add (pid, thread);

    /* and stick at end of running thread queue */
    if (running_head) {
        running_tail->next = thread;
    } else {
        running_head = thread;
        running_tail = running_head;
    }

    /* FINALLY, start the new process */
    memset(&context, 0, sizeof(context));
    context.pc = elf_getEntryPoint(elf_base);
    context.sp = stack_top;
    seL4_TCB_WriteRegisters(thread->tcb_cap, true, 0, 2, &context);

    return pid;

cleanupAS:
	addrspace_destroy (thread->as);
cleanupTCB:
	ut_free (thread->tcb_addr, seL4_TCBBits);
cleanupCSpace:
	cspace_destroy (thread->croot);
	free (thread->name);
cleanupThread:
	free (thread);
cleanupPID:
	pid_free (pid);

	return -1;
}

void threadlist_add (pid_t pid, thread_t thread) {
	threadlist[pid] = thread;
}

thread_t threadlist_lookup (pid_t pid) {
	if (pid > PID_MAX) {
		return NULL;
	}

	return threadlist[pid];
}

thread_t threadlist_first (void) {
    return running_head;
}

thread_t thread_next (thread_t t) {
    if (t) {
        return t->next;
    }

    return NULL;
}