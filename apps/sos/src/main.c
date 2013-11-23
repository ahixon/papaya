#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <cspace/cspace.h>

#include <cpio/cpio.h>
#include <pawpaw.h>

#include <syscalls.h>
#include <syscalls/syscall_table.h>

#include "ut_manager/ut.h"
#include "vm/vmem_layout.h"
#include "mapping.h"

#include <vm/vm.h>
#include <vm/addrspace.h>
#include <vm/frametable.h>

#include "thread.h"
#include <services/services.h>
#include "boot/boot.h"

#include <autoconf.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

seL4_CPtr rootserver_syscall_cap;
seL4_CPtr rootserver_async_cap;
extern seL4_CPtr _badgemap_ep;
seL4_CPtr _fs_cpio_ep;      /* XXX: move later */
extern seL4_CPtr _mmap_ep;
seL4_Word dma_addr;

seL4_CPtr swap_cap = 0;

seL4_CPtr save_reply_cap (void) {
    seL4_CPtr reply_cap = cspace_save_reply_cap (cur_cspace);
    return reply_cap;
}

void print_resource_stats (void) {
    printf ("\n************ resource allocation ************\n");
    frametable_stats ();
    printf ("Memory allocations: 0x%x\n", malloc_leak_check ());
    printf ("Root CNode free slots: 0x%x\n", cur_cspace->num_free_slots);
    ut_stats ();
    printf ("*********************************************\n\n");
}

struct pawpaw_eventhandler_info syscalls[NUM_SYSCALLS] = {
    { syscall_sbrk,             1,  HANDLER_REPLY   },
    { syscall_service_find,     3,  HANDLER_REPLY   },
    { syscall_service_register, 1,  HANDLER_REPLY   },
    { syscall_register_irq,     1,  HANDLER_REPLY   },
    { syscall_map_device,       2,  HANDLER_REPLY   },
    { syscall_alloc_cnodes,     1,  HANDLER_REPLY   },
    { syscall_create_ep_sync,   0,  HANDLER_REPLY   },
    { syscall_create_ep_async,  0,  HANDLER_REPLY   },
    { syscall_bind_async_tcb,   1,  HANDLER_REPLY   },
    { syscall_share_create,     0,  HANDLER_REPLY   },
    { syscall_share_mount,      1,  HANDLER_REPLY   },
    { syscall_share_unmount,    2,  HANDLER_REPLY   },
    { syscall_thread_suicide,   0,  0               },
    { syscall_thread_create,    2,  HANDLER_REPLY   },
    { syscall_thread_destroy,   1,  HANDLER_REPLY   },
    { syscall_thread_pid,       0,  HANDLER_REPLY   },
    { syscall_thread_list,      2,  HANDLER_REPLY   },
    { syscall_thread_wait,      1,  HANDLER_REPLY   },
    { syscall_alloc_dma,        2,  HANDLER_REPLY   },
};

struct pawpaw_event_table syscall_table = { NUM_SYSCALLS, syscalls, "sos" };

thread_t current_thread;

void syscall_event_dispose (struct pawpaw_event* evt) {
    if (evt->reply_cap) {
        cspace_delete_cap (cur_cspace, evt->reply_cap);
    }

    assert (!evt->share);

    pawpaw_event_free (evt);
}

void swap_success (struct pawpaw_event* evt) {
    thread_t thread = evt->args[1];
    assert (thread);

    /* print out the page contents */
    struct pt_entry* page = page_fetch_existing (thread->as->pagetable, evt->args[0]);
    assert (page);

    page->frame->flags &= ~FRAME_SWAPPING;

#if 0
    assert (page->cap);
    seL4_CPtr cap = cspace_copy_cap (cur_cspace, cur_cspace, page->cap, seL4_AllRights);
    assert (cap);

    int err = map_page (cap, seL4_CapInitThreadPD,
        FRAMEWINDOW_VSTART, seL4_AllRights, seL4_ARM_Default_VMAttributes);

    printf ("err = %d\n", err);
    assert (!err);

    for (int i = 0; i < PAGE_SIZE; i += 0x10) {
        printf ("%08x: ", evt->args[0] + i);

        for (int j = 0; j < 0x10; j++) {
            char* addr = FRAMEWINDOW_VSTART + i + j;
            printf ("%02x", *addr);

            if ((seL4_Word)addr % 2) {
                printf (" ");
            }
        }

        printf (" ");

        /* and char rep */
        for (int j = 0; j < 0x10; j++) {
            char* addr = FRAMEWINDOW_VSTART + i + j;
            if (*addr >= 0x20 && *addr <= 0x7e) {
                printf ("%c", *addr);
            } else {
                printf (".");
            }
        }

        printf ("\n");
    }

    seL4_ARM_Page_Unmap (cap);

#endif
    //printf ("flushing caches...\n");
    seL4_ARM_Page_FlushCaches (page->cap);

    /* wake the thread up */
    printf ("resuming thread..\n");
    seL4_Send (evt->reply_cap, evt->reply);

    //printf ("disposing..\n");
    syscall_event_dispose (evt);
}

void syscall_loop (seL4_CPtr ep) {
    while (1) {
        seL4_Word badge;
        seL4_MessageInfo_t message;

        message = seL4_Wait (ep, &badge);

        /* look for the thread associated with the syscall */
        thread_t thread = thread_lookup (badge);
        if (!thread) {
            printf ("syscall: thread lookup for %d failed\n", badge);
            continue;
        }

        current_thread = thread;

        switch (seL4_MessageInfo_get_label (message)) {
        case seL4_NoFault:
        {
            struct pawpaw_event* evt = pawpaw_event_create (message, badge);
            if (!evt) {
                printf ("syscall: failed to create event\n");
                syscall_event_dispose (evt);
                break;
            }

            /* only process valid events, and ignore everything else */
            int result = pawpaw_event_process (&syscall_table, evt, save_reply_cap);
            switch (result) {
                case PAWPAW_EVENT_NEEDS_REPLY:
                    seL4_Send (evt->reply_cap, evt->reply);
                    break;
                case PAWPAW_EVENT_HANDLED:
                    break;
                case PAWPAW_EVENT_HANDLED_SAVED:
                    /* don't dispose event since it's still used somewhere */
                    evt = NULL;
                    break;
                case PAWPAW_EVENT_INVALID:
                case PAWPAW_EVENT_UNHANDLED:
                default:
                    printf ("syscall: 0x%x failed, killing thread %s\n", seL4_GetMR (0), thread->name);
                    thread_destroy (thread);
                    break;
            }

            /* free it if we need to */
            if (evt) {
                syscall_event_dispose (evt);
            }

            break;
        }

        case seL4_VMFault:
        {
            //seL4_Word data_fault = !seL4_GetMR (2);
            vaddr_t vaddr = seL4_GetMR (1);

            /* align to page */
            vaddr &= ~(PAGE_SIZE - 1);

            /* find associated region, if any */
            struct as_region* reg = as_get_region_by_addr (thread->as, vaddr);
            if (reg) {
                /* if we're mapping inside the stack region, update our "last used" stack
                 * counter - stack grows downwards */
                if (thread->as->special_regions[REGION_STACK] == reg) {
                    if (vaddr < thread->as->stack_vaddr) {
                        thread->as->stack_vaddr = vaddr;
                    }
                }

                int status = PAGE_FAILED;
                struct pawpaw_event* evt = pawpaw_event_create (message, badge);
                if (!evt) {
                    printf ("syscall: failed to create event\n");
                    syscall_event_dispose (evt);
                    thread_destroy (thread);
                    break;
                }

                evt->reply_cap = cspace_save_reply_cap (cur_cspace);
                evt->reply = message;
                evt->args = malloc (sizeof (seL4_Word) * 2);
                evt->args[0] = vaddr;
                evt->args[1] = thread;

                printf ("%s faulted on addr 0x%x\n", thread->name, vaddr);

                /* FIXME: check if page is currently being swapped then WAIT, then try again:
                    * swap in on swapped in page should do nothing
                    * swap out on swapped in page should swap out
                    * swap in on swapped out page should swap in
                    * swap out on swapped out page should do nothing
                 */

                if (page_map (thread->as, reg, vaddr, &status, swap_success, evt)) {
                    /* restart calling thread now we have the page set */
                    //swap_success (evt);
                    seL4_Send (evt->reply_cap, evt->reply);
                    syscall_event_dispose (evt);
                    break;
                }

                if (status != PAGE_FAILED) {
                    /* waiting for page to be swapped, mmap_svc will tell us */
                    break;
                }        

                printf ("syscall: pagemap failed\n");
            } else {
                printf ("syscall: no such region for vaddr 0x%x\n", vaddr);
            }

            dprintf (0, "vm fault at 0x%08x, pc = 0x%08x, %s\n", seL4_GetMR(1),
                    seL4_GetMR(0),
                    seL4_GetMR(2) ? "Instruction Fault" : "Data fault");

            printf ("AFSR = 0x%x\n", seL4_GetMR (3));

            addrspace_print_regions (thread->as);
            pagetable_dump (thread->as->pagetable);

            dprintf (0, "killing thread %d (%s)...\n", thread->pid, thread->name);
            thread_destroy (thread);

            break;
        }

        case seL4_Interrupt:
        {
            /* ask mmap for next result in queue */
            void (*cb)(struct pawpaw_event *evt);

            seL4_MessageInfo_t req_msg = seL4_MessageInfo_new (0, 0, 0, 1);
            do {
                seL4_SetMR (0, MMAP_RESULT);
                seL4_Call (_mmap_ep, req_msg);

                cb = seL4_GetMR (0);
                seL4_Word arg = seL4_GetMR (1);

                if (cb != NULL && arg != 0) {
                    cb (arg);
                }
            } while (cb != NULL);

            break;
        }

        default:
            printf ("Rootserver got an unknown message type\n");
        }
    }
}

/*
 * Debug wrapper to interrogate untyped memory allocation
 * XXX: debug only - remove me later! 
 */
seL4_Word cspace_ut_alloc_wrapper (int sizebits) {
    return ut_alloc (sizebits);
}

/*
 * Debug wrapper to interrogate untyped memory allocation
 * XXX: debug only - remove me later! 
 */
void cspace_ut_free_wrapper (seL4_Word addr, int sizebits) {
    ut_free (addr, sizebits);
}

/*
 * Initialisation of subsystems and resources required for Papaya.
 */
static void rootserver_init (seL4_CPtr* ipc_ep, seL4_CPtr* async_ep) {
    seL4_BootInfo* _boot_info;
    seL4_Word low, high;
    int err;

    /* Retrieve boot info from seL4 */
    _boot_info = seL4_GetBootInfo ();
    conditional_panic (!_boot_info, "failed to retrieve boot info\n");

    /* Initialise the untyped sub system and reserve memory for DMA */
    err = ut_table_init (_boot_info);
    conditional_panic (err, "failed to initialise untyped table\n");

    /* DMA uses a large amount of memory that will never be freed
     * FIXME: could use allocator? do we care? */
    dma_addr = ut_steal_mem (DMA_SIZE_BITS);
    conditional_panic (dma_addr == 0, "Failed to reserve DMA memory\n");

    /* find available memory */
    ut_find_memory (&low, &high);
    high = low + (0x1000 * 1024 * 1);  /* XXX: artificially limit memory to 1 MB */

    /* Initialise the untyped memory allocator */
    ut_allocator_init (low, high);

    /* Initialise the cspace manager */
    err = cspace_root_task_bootstrap (cspace_ut_alloc_wrapper, cspace_ut_free_wrapper, ut_translate,
                                     malloc, free);
    conditional_panic (err, "failed to initialise root CSpace\n");

    /* Initialise frametable */
    frametable_init (low, high);

    /* Setup address space + pagetable for root server */
    cur_addrspace = addrspace_create (seL4_CapInitThreadPD);
    conditional_panic (!cur_addrspace, "failed to create root server address space");

    /* Initialise PID and map ID generators */
    uid_init ();

    thread_t main_thread = thread_alloc ();
    conditional_panic (!main_thread, "failed to alloc main thread struct\n");
    main_thread->name = "rootsvr";
    main_thread->as = cur_addrspace;
    main_thread->croot = cur_cspace;
    main_thread->pid = pid_next ();
    assert (main_thread->pid == 0);
    threadlist_add (main_thread->pid, main_thread);

    /* Create synchronous endpoint for process syscalls via IPC */
    seL4_Word ep_addr = ut_alloc (seL4_EndpointBits);
    conditional_panic (!ep_addr, "no memory for syscall endpoint");
    err = cspace_ut_retype_addr (ep_addr, 
                                seL4_EndpointObject, seL4_EndpointBits,
                                cur_cspace, ipc_ep);
    conditional_panic (err, "failed to retype syscall endpoint");

    /* Create async EP for ourselves */
    ep_addr = ut_alloc (seL4_EndpointBits);
    conditional_panic (!ep_addr, "no memory for rootserver async EP");
    err = cspace_ut_retype_addr (ep_addr, 
                            seL4_AsyncEndpointObject, seL4_EndpointBits,
                            cur_cspace, async_ep);
    conditional_panic (err, "failed to retype rootserver async EP");

    /* Bind so we can receive notifications on the one EP */
    err = seL4_TCB_BindAEP (seL4_CapInitThreadTCB, *async_ep);
    conditional_panic( err, "Failed to bind async EP to TCB");

    /* Start internal badge map service */
    thread_t badgemap_thread = thread_create_internal ("badgemap", cur_cspace, cur_addrspace, mapper_main);
    conditional_panic (!badgemap_thread, "failed to start badgemap");

    /* Create specific EP for badge map communication (compared to syscall EP) */
    ep_addr = ut_alloc (seL4_EndpointBits);
    conditional_panic (!ep_addr, "no memory for badgemap endpoint");
    err = cspace_ut_retype_addr (ep_addr, 
                                seL4_EndpointObject, seL4_EndpointBits,
                                cur_cspace, &_badgemap_ep);
    conditional_panic (err, "failed to retype badgemap endpoint");

    /* Create specific EP for badge map communication (compared to syscall EP) */
    ep_addr = ut_alloc (seL4_EndpointBits);
    conditional_panic (!ep_addr, "no memory for mmap endpoint");
    err = cspace_ut_retype_addr (ep_addr, 
                                seL4_EndpointObject, seL4_EndpointBits,
                                cur_cspace, &_mmap_ep);
    conditional_panic (err, "failed to retype mmap endpoint");

    /* Start mmap svc */
    thread_t mmap_thread = thread_create_internal ("mmap", cur_cspace, cur_addrspace, mmap_main);
    conditional_panic (!mmap_thread, "failed to start mmap");

    /* and do it for CPIO FS */
    ep_addr = ut_alloc (seL4_EndpointBits);
    conditional_panic (!ep_addr, "no memory for CPIO FS EP");

    thread_t fs_cpio_thread = thread_create_internal ("fs_cpio", NULL, cur_addrspace, fs_cpio_main);
    conditional_panic (!fs_cpio_thread, "failed to start fs_cpio");

    /* XXX: racey, but #yolo */
    thread_setup_default_caps (fs_cpio_thread, rootserver_syscall_cap);

    err = cspace_ut_retype_addr (ep_addr, 
                                seL4_EndpointObject, seL4_EndpointBits,
                                cur_cspace, &_fs_cpio_ep);
    conditional_panic (err, "failed to retype CPIO FS EP");

    /* FIXME: move into thread.c somewhere */
    cspace_free_slot (fs_cpio_thread->croot, PAPAYA_INITIAL_FREE_SLOT);
    seL4_CPtr last_cap = cspace_copy_cap (fs_cpio_thread->croot, cur_cspace, _fs_cpio_ep, seL4_AllRights);
    assert (last_cap == PAPAYA_INITIAL_FREE_SLOT);
}

/*
 * Main entry point - called by crt.
 */
int main (void) {
    dprintf (0, "\nPapaya starting...\n");

    /* initialise root server from whatever seL4 left us */
    rootserver_init (&rootserver_syscall_cap, &rootserver_async_cap);
    
    /* start the system boot thread - this will create all basic
     * services, and start the boot application when they're all
     * ready */    
    thread_t booter = thread_create_internal ("svc_init", cur_cspace, cur_addrspace, boot_thread);
    conditional_panic (!booter, "could not start boot thread\n");

    /* and just listen for IPCs */
    dprintf (0, "Started.\n");
    syscall_loop (rootserver_syscall_cap);

    return 0;   /* not reached */
}