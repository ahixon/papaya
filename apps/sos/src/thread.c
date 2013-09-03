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

#define DEFAULT_PRIORITY		(0)

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];

thread_t threadlist[PID_MAX] = {0};

/* FIXME: error codes would be nicer */
pid_t thread_create (char* path, seL4_CPtr reply_cap, seL4_CPtr fault_ep) {
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

	/* FIXME: actually check that path terminates! */
	thread->name = strdup (path);

    /* Create a simple 1 level CSpace */
    thread->croot = cspace_create(1);
    if (!thread->croot) {
    	goto cleanupCSpace;
    }

    /* Copy the fault endpoint to the user app to enable IPC */
    user_ep_cap = cspace_mint_cap(thread->croot, cur_cspace, fault_ep, seL4_AllRights, seL4_CapData_Badge_new (pid));
    if (!user_ep_cap) {
    	goto cleanupCSpace;
    }
    /* should be the first slot in the space, hack I know */
    /*assert(user_ep_cap == 1);
    assert(user_ep_cap == USER_EP_CAP);*/

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
    printf ("stack top = 0x%x\n", stack_top);
    printf ("stack base = 0x%x\n", stack->vbase);

    /* install into threadlist before we start */
    thread->reply_cap = reply_cap;
    threadlist_add (pid, thread);

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