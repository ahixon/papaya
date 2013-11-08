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
#include <badgemap.h>

#include <autoconf.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

#define MAPPER_STACK_SIZE 1024
#define DMA_SIZE_BITS 22

extern char _cpio_archive[];        /* FIXME: move this out of here, one day.. */

const seL4_BootInfo* _boot_info;
seL4_CPtr rootserver_syscall_cap;
extern seL4_CPtr _badgemap_ep;
seL4_Word dma_addr;

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

void syscall_loop (seL4_CPtr ep) {
    while (1) {
        seL4_Word badge;
        seL4_MessageInfo_t message;

        message = seL4_Wait (ep, &badge);

        /* look for the thread associated with the syscall */
        thread_t thread = thread_lookup (badge);
        if (!thread) {
            continue;
        }

        current_thread = thread;

        switch (seL4_MessageInfo_get_label (message)) {
        case seL4_NoFault:
        {
            struct pawpaw_event* evt = pawpaw_event_create (message, badge);
            if (!evt) {
                printf ("syscall: failed to create event\n");
                pawpaw_event_dispose (evt);
                break;
            }

            /* only process valid events, and ignore everything else */
            int result = pawpaw_event_process (&syscall_table, evt, save_reply_cap);
            switch (result) {
                case PAWPAW_EVENT_NEEDS_REPLY:
                    seL4_Send (evt->reply_cap, evt->reply);
                    pawpaw_event_dispose (evt); 
                    break;
                case PAWPAW_EVENT_HANDLED:
                    pawpaw_event_dispose (evt);
                    break;
                case PAWPAW_EVENT_HANDLED_SAVED:
                    /* don't dispose event since it's still used somewhere */
                    break;
                case PAWPAW_EVENT_INVALID:
                case PAWPAW_EVENT_UNHANDLED:
                default:
                    printf ("syscall: 0x%x failed, killing thread %s\n", seL4_GetMR (0), thread->name);
                    thread_destroy (thread);
                    break;
            }

            break;
        }

        case seL4_VMFault:
            if (!seL4_GetMR(2)) {
                /* data fault; try to map in page */
                if (as_map_page (thread->as, seL4_GetMR(1))) {
                    /* restart calling thread now we have the page set */
                    seL4_Reply (message);
                    break;
                }
            }

            dprintf (0, "vm fault at 0x%08x, pc = 0x%08x, %s\n", seL4_GetMR(1),
                    seL4_GetMR(0),
                    seL4_GetMR(2) ? "Instruction Fault" : "Data fault");

            dprintf (0, "killing thread %d (%s)...\n", thread->pid, thread->name);
            thread_destroy (thread);

            break;

        case seL4_CapFault:
        {
            char* msgs[4] = {"Invalid root", "Missing cap", "Depth mismatch", "Guard mismatch"};
            printf ("cap fault, was because of: %s\n", msgs[seL4_GetMR (3)]);
            break;
        }

        default:
            printf("Rootserver got an unknown message type\n");
        }
    }
}


static void print_bootinfo(const seL4_BootInfo* info) {
    int i;

    /* General info */
    dprintf(1, "Info Page:  %p\n", info);
    dprintf(1,"IPC Buffer: %p\n", info->ipcBuffer);
    dprintf(1,"Node ID: %d (of %d)\n",info->nodeID, info->numNodes);
    dprintf(1,"IOPT levels: %d\n",info->numIOPTLevels);
    dprintf(1,"Init cnode size bits: %d\n", info->initThreadCNodeSizeBits);

    /* Cap details */
    dprintf(1,"\nCap details:\n");
    dprintf(1,"Type              Start      End\n");
    dprintf(1,"Empty             0x%08x 0x%08x\n", info->empty.start, info->empty.end);
    dprintf(1,"Shared frames     0x%08x 0x%08x\n", info->sharedFrames.start, 
                                                   info->sharedFrames.end);
    dprintf(1,"User image frames 0x%08x 0x%08x\n", info->userImageFrames.start, 
                                                   info->userImageFrames.end);
    dprintf(1,"User image PTs    0x%08x 0x%08x\n", info->userImagePTs.start, 
                                                   info->userImagePTs.end);
    dprintf(1,"Untypeds          0x%08x 0x%08x\n", info->untyped.start, info->untyped.end);

    /* Untyped details */
    dprintf(1,"\nUntyped details:\n");
    dprintf(1,"Untyped Slot       Paddr      Bits\n");
    for (i = 0; i < info->untyped.end-info->untyped.start; i++) {
        dprintf(1,"%3d     0x%08x 0x%08x %d\n", i, info->untyped.start + i,
                                                   info->untypedPaddrList[i],
                                                   info->untypedSizeBitsList[i]);
    }

    /* Device untyped details */
    dprintf(1,"\nDevice untyped details:\n");
    dprintf(1,"Untyped Slot       Paddr      Bits\n");
    for (i = 0; i < info->deviceUntyped.end-info->deviceUntyped.start; i++) {
        dprintf(1,"%3d     0x%08x 0x%08x %d\n", i, info->deviceUntyped.start + i,
                                                   info->untypedPaddrList[i + (info->untyped.end - info->untyped.start)],
                                                   info->untypedSizeBitsList[i + (info->untyped.end-info->untyped.start)]);
    }

    dprintf(1,"-----------------------------------------\n\n");

    /* Print cpio data */
    dprintf(1,"Parsing cpio data:\n");
    dprintf(1,"--------------------------------------------------------\n");
    dprintf(1,"| index |        name      |  address   | size (bytes) |\n");
    dprintf(1,"|------------------------------------------------------|\n");
    for(i = 0;; i++) {
        unsigned long size;
        const char *name;
        void *data;

        data = cpio_get_entry(_cpio_archive, i, &name, &size);
        if(data != NULL){
            dprintf(1,"| %3d   | %16s | %p | %12d |\n", i, name, data, size);
        }else{
            break;
        }
    }
    dprintf(1,"--------------------------------------------------------\n");
}

seL4_Word cspace_ut_alloc_wrapper (int sizebits) {
    return ut_alloc (sizebits);
}

void cspace_ut_free_wrapper (seL4_Word addr, int sizebits) {
    ut_free (addr, sizebits);
}

/*
 * Initialisation of subsystems and resources required for Papaya.
 */
static void rootserver_init (seL4_CPtr* ipc_ep){
    seL4_Word low, high;
    int err;

    /* Retrieve boot info from seL4 */
    _boot_info = seL4_GetBootInfo ();
    conditional_panic (!_boot_info, "failed to retrieve boot info\n");
    //print_bootinfo (_boot_info);

    /* Initialise the untyped sub system and reserve memory for DMA */
    err = ut_table_init (_boot_info);
    conditional_panic (err, "failed to initialise untyped table\n");

    /* DMA uses a large amount of memory that will never be freed
     * FIXME: could use allocator? do we care? */
    dma_addr = ut_steal_mem (DMA_SIZE_BITS);
    conditional_panic (dma_addr == 0, "Failed to reserve DMA memory\n");

    /* find available memory */
    ut_find_memory (&low, &high);
    //high = high / 1024 / 1024 / 1024;    /* XXX: half available memory for swapping tests */
    high = low + (0x1000 * 1024 * 10);  /* 10 MB */

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

    /* and map in an IPC for internal services so that thread_create_internal works */
    if (!as_define_region (cur_addrspace, PROCESS_IPC_BUFFER, PAGE_SIZE, seL4_AllRights, REGION_IPC)) {
        panic ("failed to define IPC region for internal processes");
    }

    if (!as_map_page (cur_addrspace, PROCESS_IPC_BUFFER)) {
        panic ("failed to map IPC region for internal processes");
    }

    /*if (!as_define_region (cur_addrspace, PROCESS_IPC_BUFFER, PAGE_SIZE, seL4_AllRights, REGION_IPC)) {
        panic ("failed to define IPC region for internal processes");
    }*/

    /* Initialise PID and map ID generators */
    uid_init ();

    /* Create synchronous endpoint for process syscalls via IPC */
    seL4_Word ep_addr = ut_alloc (seL4_EndpointBits);
    conditional_panic (!ep_addr, "no memory for syscall endpoint");
    err = cspace_ut_retype_addr (ep_addr, 
                                seL4_EndpointObject, seL4_EndpointBits,
                                cur_cspace, ipc_ep);
    conditional_panic (err, "failed to retype syscall endpoint");

    /* Start internal badge map service */
    thread_t badgemap_thread = thread_create_internal ("badgemap", mapper_main, MAPPER_STACK_SIZE);
    conditional_panic (!badgemap_thread, "failed to start badgemap");

    /* Create specific EP for badge map communication (compared to syscall EP) */
    ep_addr = ut_alloc (seL4_EndpointBits);
    conditional_panic (!ep_addr, "no memory for badgemap endpoint");
    err = cspace_ut_retype_addr (ep_addr, 
                                seL4_EndpointObject, seL4_EndpointBits,
                                cur_cspace, &_badgemap_ep);
    conditional_panic (err, "failed to retype badgemap endpoint");
}

/*
 * Main entry point - called by crt.
 */
int main (void) {
    dprintf (0, "\nPapaya starting...\n");

    /* initialise root server from whatever seL4 left us */
    rootserver_init (&rootserver_syscall_cap);
    //printf ("Root server setup.\n");
    // print_resource_stats ();

    /* start the system boot thread - this will create all basic
     * services, and start the boot application when they're all
     * ready */    
    thread_t booter = thread_create_internal ("svc_init", boot_thread, MAPPER_STACK_SIZE))
    conditional_panic (!booter, "could not start svc_init\n");

    /* wait for IPC from <homestar>everyboooddddyyyyy</homestar> */
    //dprintf (0, "Root server starting event loop...\n");
    //print_resource_stats ();

    dprintf (0, "Started.\n");
    syscall_loop (rootserver_syscall_cap);

    return 0;   /* not reached */
}