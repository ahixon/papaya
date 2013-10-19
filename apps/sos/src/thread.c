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

void print_resource_stats (void);

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
        ut_free (service_addr, seL4_EndpointBits);
        return 0;
    }

    struct thread_resource *res = malloc (sizeof (struct thread_resource));
    if (!res) {
        cspace_delete_cap (thread->croot, service_cap);
        ut_free (service_addr, seL4_EndpointBits);
        return 0;
    }

    res->addr = service_addr;
    res->next = thread->resources;
    res->size = seL4_EndpointBits;
    thread->resources = res;

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
        ut_free (aep_addr, seL4_EndpointBits);
        return 0;
    }

    struct thread_resource *res = malloc (sizeof (struct thread_resource));
    if (!res) {
        cspace_delete_cap (thread->croot, async_ep);
        ut_free (aep_addr, seL4_EndpointBits);
        return 0;
    }

    res->addr = aep_addr;
    res->next = thread->resources;
    res->size = seL4_EndpointBits;
    thread->resources = res;

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
        printf ("%s: inital slot alloc was null\n", __FUNCTION__);
        return 0;
    }

    seL4_CPtr prev_cap = cap;
    int alloc = 1;

    while (alloc < num) {
        seL4_CPtr cur_cap = cspace_alloc_slot (thread->croot);
        if (cur_cap == CSPACE_NULL) {
            /* no more room left */
            printf ("%s: ran out of free slots after %d\n", __FUNCTION__, alloc);
            break;
        }

        if (cur_cap - 1 != prev_cap) {
            /* no longer contiguous, free just-allocated slot and return */
            cspace_free_slot (thread->croot, cur_cap);
            break;
        }

        alloc++;
    }

    //printf ("%s: allocated %d contiguous slots\n", __FUNCTION__, alloc);

    *cnode = cap;
    return alloc;
}

static thread_t
thread_alloc (void) {
    thread_t thread = malloc (sizeof (struct thread));
    if (!thread) {
        return NULL;
    }

    memset (thread, 0, sizeof (struct thread));
    return thread;
}

thread_t thread_create (char* name, cspace_t *existing_cspace, addrspace_t existing_addrspace) {
    thread_t thread = thread_alloc ();
    if (!thread) {
        return NULL;
    }

    pid_t pid = pid_next ();
    if (pid < 0) {
        printf ("%s: no more PIDs\n", __FUNCTION__);
        /* no more process IDs left */
        thread_destroy (thread);
        return NULL;
    }

    thread->pid = pid;
    thread->name = strdup (name);
    thread->next = NULL;

    /* Create a new TCB object */
    thread->tcb_addr = ut_alloc (seL4_TCBBits);
    if (!thread->tcb_addr) {
        thread_destroy (thread);
        return NULL;
    }

    if (cspace_ut_retype_addr (thread->tcb_addr,
                               seL4_TCBObject, seL4_TCBBits,
                               cur_cspace, &thread->tcb_cap)) {
        printf ("%s: TCB retype failed\n", __FUNCTION__);
        thread_destroy (thread);
    }

    if (!existing_addrspace) {
        /* create address space for process */
        thread->as = addrspace_create (0);
        if (!thread->as) {
            printf ("%s: failed to create addrspace\n", __FUNCTION__);
            thread_destroy (thread);
            return NULL;
        }

        /* Map in IPC first off (since we need it for TCB configuration) */
        as_define_region (thread->as, PROCESS_IPC_BUFFER, PAGE_SIZE, seL4_AllRights, REGION_IPC);
        if (!as_map_page (thread->as, PROCESS_IPC_BUFFER)) {
            printf ("%s: failed to map IPC\n", __FUNCTION__);
            thread_destroy (thread);
            return NULL;
        }
    } else {
        thread->as = existing_addrspace;
        /* assume that if we already have an address space, then IPC should already be
         * mapped in */
    }

    seL4_CPtr ipc_cap = as_get_page_cap (thread->as, PROCESS_IPC_BUFFER);
    if (!ipc_cap) {
        printf ("%s: failed to get IPC page cap\n", __FUNCTION__);
        thread_destroy (thread);
        return NULL;
    }

    if (!existing_cspace) {
        /* Create thread's CSpace (which we will manage in-kernel - although they
         * get to manage any empty CNodes they request.
         *
         * Apparently, we must have a level 2 CSpace otherwise the thread can't
         * store caps it receives from other threads via IPC. Bug in seL4? or the
         * way libsel4cspace creates the CSpace?
         */
        thread->croot = cspace_create (2);
        if (!thread->croot) {
            printf ("%s: failed to create cspace\n", __FUNCTION__);
            thread_destroy (thread);
            return NULL;
        }
    } else {
        thread->croot = existing_cspace;
    }

    int err = seL4_TCB_Configure(thread->tcb_cap, PAPAYA_SYSCALL_SLOT, DEFAULT_PRIORITY,
                             thread->croot->root_cnode, seL4_NilData,
                             thread->as->pagedir_cap, seL4_NilData, PROCESS_IPC_BUFFER,
                             ipc_cap);
    if (err) {
        printf ("%s: failed to configure TCB: 0x%x\n", __FUNCTION__, err);
        thread_destroy (thread);
        return NULL;
    }

    return thread;
}

void 
thread_destroy (thread_t thread) {
    if (thread->name) {
        free (thread->name);
    }

    if (thread->static_stack) {
        free (thread->static_stack);
    }

    if (thread->pid >= PID_MIN) {
        threadlist[thread->pid] = 0;
        pid_free (thread->pid);
    }

    if (thread->croot && thread->croot != cur_cspace) {
        cspace_destroy (thread->croot);
    }

    if (thread->tcb_cap) {
        cspace_delete_cap (cur_cspace, thread->tcb_cap);
    }

    if (thread->tcb_addr) {
        ut_free (thread->tcb_addr, seL4_TCBBits);
    }

    /* free any created resources (specifically, endpoints) */
    struct thread_resource *res = thread->resources;
    while (res) {
        ut_free (res->addr, res->size);

        struct thread_resource *next = res->next;
        free (res);
        res = next;
    }

    /* lastly, free the address space ONLY if we're not rootsvr's */
    if (thread->as && thread->as != cur_addrspace) {
        /* will free underlying pages + frames */
        addrspace_destroy (thread->as);
    }

    /* now, notify everyone about thread's death (and their presents) */
    struct pawpaw_saved_event* bequests = thread->bequests;
    while (bequests) {
        struct pawpaw_event* evt = bequests->evt;

        /* FIXME: should save response args in event struct too */
        seL4_SetMR (0, thread->pid);
        seL4_Send (evt->reply_cap, evt->reply);
        pawpaw_event_dispose (evt); 

        struct pawpaw_saved_event* next_bequest = bequests->next;
        free (bequests);
        bequests = next_bequest;
    }

    /* FIXME: free known services - MORE OVER WHY IS THAT HERE */
    
    /* FIXME: this takes O(n) which sucks, but we only do it on thread
     * deletion... - maybe have a prev pointer too? */
    thread_t prev = running_head;
    while (prev) {
        if (prev->next == thread) {
            break;
        }

        prev = prev->next;
    }

    /* update next */
    if (prev) {
        prev->next = thread->next;
    } else {
        /* we must've been first */
        running_head = thread->next;
    }

    free (thread);

    /* TODO: remove me, just for debugging */
    print_resource_stats ();
}

int thread_setup_default_caps (thread_t thread, seL4_CPtr rootsvr_ep) {
     /* Copy a whole bunch of default caps to their cspace, namely:
     *  - a minted reply cap (with their PID), so that they can do IPC to the
     *    root server
     *  - their TCB cap
     *  - their root CNode cap
     *  - their pagedir cap
     * 
     *  (in that order)
     */

    seL4_Word last_cap;

    last_cap = cspace_mint_cap(thread->croot, cur_cspace, rootsvr_ep, seL4_AllRights, seL4_CapData_Badge_new (thread->pid));
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

    return true;
}

int thread_rename (thread_t thread, char* name) {
    free (thread->name);
    thread->name = strdup (name);

    return (thread->name != NULL);
}

thread_t thread_create_from_fs (char* path, seL4_CPtr rootsvr_ep) {
    return thread_create_from_cpio (path, rootsvr_ep);
}

thread_t thread_create_from_cpio (char* path, seL4_CPtr rootsvr_ep) {
    char* elf_base;
    unsigned long elf_size;

    thread_t thread = thread_create (path, NULL, NULL);
    if (!thread) {
        return NULL;
    }

    /* install caps that threads usually expect */
    if (!thread_setup_default_caps (thread, rootsvr_ep)) {
        thread_destroy (thread);
        return NULL;
    }

    /* FIXME: look up using read */
    dprintf (1, "Starting \"%s\"...\n", path);
    elf_base = cpio_get_file (_cpio_archive, path, &elf_size);
    if (!elf_base) {
        thread_destroy (thread);
        return NULL;
    }

    /* load the elf image */
    if (elf_load (thread->as, elf_base)) {
        thread_destroy (thread);
        return NULL;
    }

    /* find where we put the stack */
    struct as_region* stack = as_get_region_by_type (thread->as, REGION_STACK);
    vaddr_t stack_top = stack->vbase + stack->size;

    /*printf ("Thread's address space looks like:\n");
    addrspace_print_regions (thread->as);*/

    /* install into threadlist before we start */
    threadlist_add (thread->pid, thread);

    /* and stick at end of running thread queue */
    /* FIXME: what is this shit */
    if (running_tail) {
        running_tail->next = thread;
        running_tail = thread;
    } else {
        running_head = thread;
        running_tail = running_head;
    }

    /* FINALLY, start the new process */
    seL4_TCB_WritePCSP (thread->tcb_cap, true, elf_getEntryPoint(elf_base), stack_top);
    return thread;
}

thread_t
thread_create_internal (char* name, void* initial_pc, unsigned int stack_size) {
    thread_t thread = thread_create (name, cur_cspace, cur_addrspace);
    if (!thread) {
        return NULL;
    }

    char* stack = malloc (stack_size);
    if (!stack) {
        thread_destroy (thread);
        return NULL;
    }

    thread->static_stack = stack;

    seL4_TCB_WritePCSP (thread->tcb_cap, true, (seL4_Word)initial_pc, (seL4_Word)&stack[stack_size]);
    return thread;
}

void threadlist_add (pid_t pid, thread_t thread) {
	threadlist[pid] = thread;
}

thread_t thread_lookup (pid_t pid) {
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