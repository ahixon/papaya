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
#include <elf.h>
#include "elf.h"

#include "vm/vmem_layout.h"
#include <vfs.h>
#include <sos.h>
#include <vm/frametable.h>

#include <cpio/cpio.h>

#include <services/services.h>
#include <syscalls/syscall_table.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>
#include <assert.h>

#define DEFAULT_PRIORITY		(0)

/* 64KB ought to be enough for everybody */
#define INTERNAL_STACK_SIZE     (64*1024)

void print_resource_stats (void);

extern seL4_CPtr _badgemap_ep;      /* XXX: move later */
extern seL4_CPtr _fs_cpio_ep;      /* XXX: move later */

extern char _cpio_archive[];

thread_t threadlist[PID_MAX] = {0};

thread_t running_head = NULL;

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

        /* FIXME: add to thread_resources list since they might need freeing */
        alloc++;
    }

    *cnode = cap;
    return alloc;
}

thread_t
thread_alloc (void) {
    thread_t thread = malloc (sizeof (struct thread));
    if (!thread) {
        return NULL;
    }

    memset (thread, 0, sizeof (struct thread));
    return thread;
}

thread_t thread_create (char* name, cspace_t *existing_cspace,
    addrspace_t existing_addrspace) {

    thread_t thread = thread_alloc ();
    if (!thread) {
        return NULL;
    }

    pid_t pid = pid_next ();
    if (pid < 0) {
        /* no more process IDs left */
        thread_destroy (thread);
        return NULL;
    }

    thread->pid = pid;
    thread->name = strdup (name);
    thread->next = NULL;

    /* so that thread.c doesn't depend on svc_timer */
    thread->start = 0;  

    /* Create a new TCB object */
    thread->tcb_addr = ut_alloc (seL4_TCBBits);
    if (!thread->tcb_addr) {
        thread_destroy (thread);
        return NULL;
    }

    if (cspace_ut_retype_addr (thread->tcb_addr,
                               seL4_TCBObject, seL4_TCBBits,
                               cur_cspace, &thread->tcb_cap)) {
        thread_destroy (thread);
    }

    if (!existing_addrspace) {
        /* create address space for process */
        thread->as = addrspace_create (0);
        if (!thread->as) {
            thread_destroy (thread);
            return NULL;
        }
    } else {
        thread->as = existing_addrspace;
        /* assume that if we already have an address space, then IPC should
         * already be mapped in */
    }

    /* Map in IPC first off (since we need it for TCB configuration) */
    struct as_region* ipc_reg = as_define_region_within_range (thread->as,
            PROCESS_IPC_BUFFER, PROCESS_IPC_BUFFER_END, PAGE_SIZE,
            seL4_AllRights, REGION_IPC);

    int status = PAGE_FAILED;

    /* FIXME: should actually have a callback since this may fail */
    struct pt_entry* page = page_map (thread->as, ipc_reg, ipc_reg->vbase,
        &status, NULL, NULL);

    assert (status == PAGE_SUCCESS);

    if (!page) {
        thread_destroy (thread);
        return NULL;
    }

    /* don't swap thread's seL4 IPC page please */
    page->frame->flags |= FRAME_PINNED;

    assert (page->cap);
    seL4_CPtr ipc_cap = page->cap;

    if (!existing_cspace) {
        /* Create thread's CSpace (which we will manage in-kernel - although 
         * they get to manage any empty CNodes they request.
         *
         * Apparently, we must have a level 2 CSpace otherwise the thread can't
         * store caps it receives from other threads via IPC. Bug in seL4? or
         * the way libsel4cspace creates the CSpace?
         */
        thread->croot = cspace_create (2);
        if (!thread->croot) {
            thread_destroy (thread);
            return NULL;
        }
    } else {
        thread->croot = existing_cspace;
    }

    int err = seL4_TCB_Configure (thread->tcb_cap, PAPAYA_SYSCALL_SLOT,
                            DEFAULT_PRIORITY, thread->croot->root_cnode,
                            seL4_NilData, thread->as->pagedir_cap, seL4_NilData,
                            ipc_reg->vbase, ipc_cap);
    if (err) {
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

    /* why doesn't the cspace free allocated nodes on destroy, we'll never know
     * let's not modify that code because it's a giant mess */
    if (thread->default_caps) {
        for (int i = PAPAYA_SYSCALL_SLOT; i <= PAPAYA_PAGEDIR_SLOT; i++) {
            cspace_delete_cap (thread->croot, i);
        }

        cspace_free_slot (thread->croot, PAPAYA_INITIAL_FREE_SLOT);
    }

    /* FIXME: for some reason NFS has the syscall cap in their CSpace?
     * Related to IRQ bug? */
    if (thread->croot && thread->croot != cur_cspace) {
        //printf ("destroying thread croot\n");
        //cspace_destroy (thread->croot);
        //printf ("done\n");
    }

#if 0
    /* lastly, free the address space ONLY if we're not rootsvr's */
    if (thread->as && thread->as != cur_addrspace) {
        /* will free underlying pages + frames */
        addrspace_destroy (thread->as);
    }
#endif

    /* free any created resources (specifically, endpoints) */
    struct thread_resource *res = thread->resources;
    while (res) {
#if 0
        /* XXX: fucking fantastic, seL4... */
        ut_free (res->addr, res->size);
#endif

        struct thread_resource *next = res->next;
        free (res);
        res = next;
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
    
    /* FIXME: this takes O(n) which sucks, but we only do it on thread
     * deletion... - maybe have a prev pointer too? */
    thread_t cur = running_head;
    thread_t prev = NULL;
    while (cur && !prev) {
        if (cur->next == thread) {
            prev = cur;
        }

        cur = cur->next;
    }

    /* update next */
    if (prev) {
        prev->next = thread->next;
    } else {
        /* we must've been first */
        running_head = thread->next;
    }

    /* XXX: hack until seL4 bug fixed 
    cspace_delete_cap (cur_cspace, thread->tcb_cap);

    if (thread->tcb_cap) {
        cspace_delete_cap (cur_cspace, thread->tcb_cap);
        printf ("%s:%d\n", __FILE__, __LINE__);
    }

    if (thread->tcb_addr) {
        ut_free (thread->tcb_addr, seL4_TCBBits);
    }*/
    
    free (thread);
}

/* FIXME: should not assert */
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

    last_cap = cspace_mint_cap(thread->croot, cur_cspace, rootsvr_ep,
        seL4_AllRights, seL4_CapData_Badge_new (thread->pid));

    assert (last_cap == PAPAYA_SYSCALL_SLOT);

    last_cap = cspace_copy_cap (thread->croot, cur_cspace, thread->tcb_cap,
        seL4_AllRights);

    assert (last_cap == PAPAYA_TCB_SLOT);

    last_cap = cspace_copy_cap (thread->croot, cur_cspace,
        thread->croot->root_cnode, seL4_AllRights);

    assert (last_cap == PAPAYA_ROOT_CNODE_SLOT);

    last_cap = cspace_copy_cap (thread->croot, cur_cspace,
        thread->as->pagedir_cap, seL4_AllRights);

    assert (last_cap == PAPAYA_PAGEDIR_SLOT);
    
    /* Now, allocate an initial free slot */
    last_cap = cspace_alloc_slot (thread->croot);
    assert (last_cap == PAPAYA_INITIAL_FREE_SLOT);

    thread->default_caps = true;

    return true;
}

/* creates a new thread using lazy loading (file is memory mapped)
 * requires that first page of file to be read in already */
thread_t thread_create_from_fs (char* name, char *file, seL4_CPtr file_cap,
    int file_size, seL4_CPtr rootsvr_ep) {

    assert (file);

    thread_t thread = thread_create (name, NULL, NULL);
    if (!thread) {
        return NULL;
    }

    /* install caps that threads usually expect */
    if (!thread_setup_default_caps (thread, rootsvr_ep)) {
        thread_destroy (thread);
        return NULL;
    }

    /* make sure we don't have some crafty file that tries to
     * overrun our buffer, or isn't an ELF at all */

    if (elf_checkFile (file)) {
        thread_destroy (thread);
        return NULL;
    }

    unsigned long sect_end = elf_getProgramHeaderOffset (file,
        elf_getNumProgramHeaders (file) - 1);
    
    /* we only read in one page - ensure the program headers are inside */
    if (sect_end > PAGE_SIZE) {
        /* program headers were too big for one read, not worth it */
        thread_destroy (thread);
        return NULL;
    }

    /* ok looks good should be able to read everything in */
    seL4_Word entry_point = elf_getEntryPoint (file);

    int num_headers = elf_getNumProgramHeaders (file);
    for (int i = 0; i < num_headers; i++) {
        unsigned long flags, segment_file_size, segment_size, vaddr, offset;

        /* skip non-loadable segments (eg debugging data) */
        if (elf_getProgramHeaderType (file, i) != PT_LOAD) {
            continue;
        }

        /* fetch information about this segment */
        offset = elf_getProgramHeaderOffset (file, i);
        segment_file_size = elf_getProgramHeaderFileSize (file, i);
        segment_size = elf_getProgramHeaderMemorySize (file, i);
        vaddr = elf_getProgramHeaderVaddr (file, i);
        flags = elf_getProgramHeaderFlags (file, i);

        /* mmap the file */
        if (!load_segment_into_vspace (thread->as, file_cap, offset,
            segment_size, segment_file_size, vaddr,
            get_sel4_rights_from_elf (flags) & seL4_AllRights)) {

            thread_destroy (thread);
            return NULL;
        }
    }

    /* FIXME: free the opened file buf ON THREAD DESTROY */

    if (as_create_stack_heap (thread->as, NULL, NULL)) {
        thread_destroy (thread);
        return NULL;
    }

    /* install into threadlist before we start */
    threadlist_add (thread->pid, thread);

    /* and stick at end of running thread queue */
    if (!running_head) {
        running_head = thread;
    } else {
        thread_t end = running_head;
        while (end->next) {
            end = end->next;
        }

        end->next = thread;
    }
    
    /* finally, start the new process */
    seL4_TCB_WritePCSP (thread->tcb_cap, true, entry_point,
        thread->as->stack_vaddr);

    return thread;
}

struct as_region* share_reg = NULL;
seL4_CPtr share_badge = 0;
seL4_Word share_id = 0;

thread_t thread_create_from_fs_oneblock (char* path, seL4_CPtr rootsvr_ep, void* cb, struct pawpaw_event *oldevt) {
    /* FIXME: make common with syscall_share.c */
    if (!share_reg) {
        share_reg = create_share_reg (&share_badge, &share_id, true);
    }

    struct pawpaw_event *evt = malloc (sizeof (struct pawpaw_event));
    assert (evt);

    int len = strlen (path);
    memcpy ((char*)share_reg->vbase, path, len);
    *(char*)(share_reg->vbase + len) = '\0';

    evt->args = malloc (sizeof (seL4_Word) * 6);
    evt->args[0] = (seL4_Word)path;
    evt->args[1] = share_reg->vbase;
    //evt->args[2] = recv_cap;
    evt->args[3] = 0;
    evt->args[4] = rootsvr_ep;
    evt->args[5] = (seL4_Word)oldevt;

    mmap_swap (BUFFER_OPEN_LOAD, share_reg->vbase, (struct frameinfo*)path, cb, evt);
    return NULL;
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

    if (as_create_stack_heap (thread->as, NULL, NULL)) {
        thread_destroy (thread);
        return NULL;
    }

    /* install into threadlist before we start */
    threadlist_add (thread->pid, thread);

    /* and stick at end of running thread queue */
    if (!running_head) {
        running_head = thread;
    } else {
        thread_t end = running_head;
        while (end->next) {
            end = end->next;
        }

        end->next = thread;
    }

    /* FINALLY, start the new process */
    seL4_TCB_WritePCSP (thread->tcb_cap, true, elf_getEntryPoint(elf_base),
        thread->as->stack_vaddr);

    return thread;
}

thread_t
thread_create_internal (char* name, cspace_t *existing_cspace,
    addrspace_t existing_addrspace, void* initial_pc) {

    thread_t thread = thread_create (name, existing_cspace, existing_addrspace);
    if (!thread) {
        return NULL;
    }

    char* stack = malloc (INTERNAL_STACK_SIZE);
    if (!stack) {
        thread_destroy (thread);
        return NULL;
    }

    thread->static_stack = stack;

    /* install into threadlist before we start */
    threadlist_add (thread->pid, thread);

    /* DON'T add to running list to hide it */

    seL4_TCB_WritePCSP (thread->tcb_cap, true, (seL4_Word)initial_pc,
        (seL4_Word)&stack[INTERNAL_STACK_SIZE]);
    
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

void thread_pin (thread_t thread) {
    thread->pinned = true;
    thread->as->pinned = true;

    /* walk PT and pin pages */
    pagetable_pin (thread->as->pagetable);
}