#include <stdlib.h>
#include <string.h>

#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>

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

/* FIXME: error codes would be nicer */
pid_t thread_create (char* path, seL4_CPtr reply_cap) {
	int err;
    seL4_CPtr user_ep_cap;
    seL4_UserContext context;

    char* elf_base;
    unsigned long elf_size;

    pid_t pid = pid_next();
    if (pid < 0) {
    	goto cleanupPID;
    }

	thread_t thread = malloc (sizeof (struct thread));
	if (!thread) {
		goto cleanupThread;
	}

    thread->next = NULL;

	/* FIXME: actually check that path terminates! */
	thread->name = strdup (path);

    thread->croot = cspace_create(2);           /* MUST BE 2 LEVEL otherwise we cannot retype into this cspace */
    if (!thread->croot) {
    	goto cleanupCSpace;
    }

    /* Copy the fault endpoint to the user app to enable IPC */
    user_ep_cap = cspace_mint_cap(thread->croot, cur_cspace, reply_cap, seL4_AllRights | seL4_Transfer_Mint, seL4_CapData_Badge_new (pid));
    printf ("*** Minted EP %x with badge %d to %x in process cspace\n", reply_cap, pid, user_ep_cap);
    if (!user_ep_cap) {
    	goto cleanupCSpace;
    }
    /* should be the first slot in the space, hack I know */
    assert(user_ep_cap == 1);
    //assert(user_ep_cap == USER_EP_CAP);

    seL4_CPtr reserved_cap = cspace_alloc_slot (thread->croot);
    if (!reserved_cap) {
        goto cleanupCSpace;
    }

    printf ("Got reserved cap\n");
    assert (reserved_cap == 2);

    seL4_CPtr service_cap;

    seL4_Word service_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!service_addr, "No memory for endpoint");
    err = cspace_ut_retype_addr(service_addr, 
                                seL4_EndpointObject,
                                seL4_EndpointBits,
                                //thread->croot,
                                cur_cspace,
                                &service_cap);
    conditional_panic(err, "Failed to allocate c-slot for IPC endpoint");

    seL4_CPtr their_service_cap = cspace_copy_cap (thread->croot, cur_cspace, service_cap, seL4_AllRights);
    conditional_panic(!their_service_cap, "could not copy service cap for app");

    printf ("had service IPC (them = 0x%x, us = 0x%x)\n", their_service_cap, service_cap);

    /* Create a new TCB object */
    thread->tcb_addr = ut_alloc(seL4_TCBBits);
    if (!thread->tcb_addr) {
    	goto cleanupTCB;
    }

    if (cspace_ut_retype_addr(thread->tcb_addr, seL4_TCBObject, seL4_TCBBits, cur_cspace, &thread->tcb_cap)) {
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

    /* Configure the TCB */
    printf ("FYI: root cnode is %d\n", thread->croot->root_cnode);

    seL4_CPtr root_copy = cspace_copy_cap (thread->croot, cur_cspace, thread->croot->root_cnode, seL4_AllRights);
    printf ("And root copy = %d\n", root_copy);

    seL4_CPtr async_ep;

    seL4_Word aep_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!aep_addr, "No memory for async endpoint");
    err = cspace_ut_retype_addr(aep_addr,
                                seL4_AsyncEndpointObject,
                                seL4_EndpointBits,
                                thread->croot,
                                &async_ep);
    conditional_panic(err, "Failed to allocate c-slot for Interrupt endpoint");

    printf ("and async ep is %d\n", async_ep);

    seL4_CPtr tcb_copy = cspace_copy_cap (thread->croot, cur_cspace, thread->tcb_cap, seL4_AllRights);
    printf ("and tcb copy is %d\n", tcb_copy);

    err = seL4_TCB_Configure(thread->tcb_cap, user_ep_cap, DEFAULT_PRIORITY,
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
    /*printf ("stack top = 0x%x\n", stack_top);
    printf ("stack base = 0x%x\n", stack->vbase);*/

    /* install into threadlist before we start */
    thread->reply_cap = service_cap;
    threadlist_add (pid, thread);

    /* and stick at end of running thread queue */
    if (running_head) {
        running_tail->next = thread;
    } else {
        running_head = thread;
        running_tail = running_head;
    }

    /* Start the new process */
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