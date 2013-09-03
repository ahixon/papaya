#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <cspace/cspace.h>

#include <cpio/cpio.h>
#include <nfs/nfs.h>
#include <serial/serial.h>
#include <clock/clock.h>
#include <syscalls.h>

#include "network.h"

#include "ut_manager/ut.h"
#include "vm/vmem_layout.h"
#include "mapping.h"

#include "vm/vmem_layout.h"

#include <vm/vm.h>
#include <vm/addrspace.h>
#include <vm/frametable.h>

#include "thread.h"

#include <autoconf.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

#define IPC_TIMER_BADGE      (102)

extern char _cpio_archive[];

const seL4_BootInfo* _boot_info;

seL4_CPtr _sos_ipc_ep_cap;
seL4_CPtr _sos_interrupt_ep_cap;

struct serial* ser_device;

/**
 * NFS mount point
 */
extern fhandle_t mnt_point;


void handle_syscall(thread_t thread, int num_args) {
    seL4_Word syscall_number;
    seL4_CPtr reply_cap;
    seL4_MessageInfo_t reply;


    syscall_number = seL4_GetMR(0);

    /* Save the caller */
    reply_cap = cspace_save_reply_cap(cur_cspace);
    assert(reply_cap != CSPACE_NULL);

    /* Process system call */
    switch (syscall_number) {
    case SYSCALL_NETWRITE:
        if (num_args < 0) {
            break;
        }

        int sent;
        char* data = malloc (sizeof (char) * num_args);

        if (data != NULL) {
            for (int i = 0; i < num_args; i++) {
                data[i] = (char)seL4_GetMR (i + 1);
            }

            sent = serial_send (ser_device, data, num_args);
            free (data);
        } else {
            dprintf (0, "syscall: could not allocate memory for serial buffer\n");
            /* malloc failed, so return negative status code
             * so that client doesn't try to re-send */
            sent = -1;
        }
        
        reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, sent);
        seL4_Send(reply_cap, reply);

        cspace_free_slot(cur_cspace, reply_cap);

        break;

    case SYSCALL_SBRK:
        if (num_args < 0) {
            break;
        }

        printf ("syscall: asked for sbrk\n");
        vaddr_t new_addr = as_resize_heap (thread->as, seL4_GetMR (1));

        reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, new_addr);
        seL4_Send(reply_cap, reply);

        cspace_free_slot(cur_cspace, reply_cap);
        break;

    default:
        printf("Unknown syscall %d\n", syscall_number);
        /* we don't want to reply to an unknown syscall */

    }
}

void syscall_loop(seL4_CPtr ep) {

    while (1) {
        seL4_Word badge;
        seL4_Word interrupts_fired;
        seL4_MessageInfo_t message;

        message = seL4_Wait(ep, &badge);

        thread_t thread;

        switch (seL4_MessageInfo_get_label(message)) {
        case seL4_NoFault:
            thread = threadlist_lookup (badge);
            if (!thread) {
                printf ("syscall: invalid thread - had badge %d\n", badge);
                break;
            }

            //printf ("had syscall from %s\n", thread->name);
            handle_syscall(thread, seL4_MessageInfo_get_length(message) - 1);
            break;

        case seL4_VMFault:
            thread = threadlist_lookup (badge);
            if (!thread) {
                printf ("syscall: invalid thread - had badge %d\n", badge);
                break;
            }

            if (!seL4_GetMR(2)) {
                /* data fault; try to map in page */
                if (as_map_page (thread->as, seL4_GetMR(1))) {
                    /* restart calling thread now we have the page set */
                    seL4_Reply (message);
                    break;
                }
            }

            /*dprintf(0, "vm fault at 0x%08x, pc = 0x%08x, %s\n", seL4_GetMR(1),
                    seL4_GetMR(0),
                    seL4_GetMR(2) ? "Instruction Fault" : "Data fault");*/
            printf ("not waking thread %d (%s)...\n", thread->pid, thread->name);

            //assert(!"Unable to handle vm faults");
            break;

        case seL4_Interrupt:
            /* in the case of interrupt(s), all interrupt
             * numbers that are being delivered
             * are orred together in message register 0.
             */
            interrupts_fired = seL4_GetMR(0);

            if (badge & IPC_TIMER_BADGE) {
                handle_timer();
            }

            network_irq(interrupts_fired);
            break;

        default:
            printf("Rootserver got an unknown message\n");
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
    /*dprintf(1,"Parsing cpio data:\n");
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
    dprintf(1,"--------------------------------------------------------\n");*/
}

static void _sos_ipc_init(seL4_CPtr* ipc_ep, seL4_CPtr* async_ep){
    seL4_Word ep_addr, aep_addr;
    int err;

    /* Create an Async endpoint for interrupts */
    aep_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!aep_addr, "No memory for async endpoint");
    err = cspace_ut_retype_addr(aep_addr,
                                seL4_AsyncEndpointObject,
                                seL4_EndpointBits,
                                cur_cspace,
                                async_ep);
    conditional_panic(err, "Failed to allocate c-slot for Interrupt endpoint");

    /* Bind the Async endpoint to our TCB */
    err = seL4_TCB_BindAEP(seL4_CapInitThreadTCB, *async_ep);
    conditional_panic(err, "Failed to bind ASync EP to TCB");


    /* Create an endpoint for user application IPC */
    ep_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!ep_addr, "No memory for endpoint");
    err = cspace_ut_retype_addr(ep_addr, 
                                seL4_EndpointObject,
                                seL4_EndpointBits,
                                cur_cspace,
                                ipc_ep);
    conditional_panic(err, "Failed to allocate c-slot for IPC endpoint");
}


static void _sos_init(seL4_CPtr* ipc_ep, seL4_CPtr* async_ep){
    seL4_Word dma_addr;
    seL4_Word low, high;
    int err;

    /* Retrieve boot info from seL4 */
    _boot_info = seL4_GetBootInfo();
    conditional_panic(!_boot_info, "Failed to retrieve boot info\n");
    if(verbose > 0){
        print_bootinfo(_boot_info);
    }

    /* Initialise the untyped sub system and reserve memory for DMA */
    err = ut_table_init(_boot_info);
    conditional_panic(err, "Failed to initialise Untyped Table\n");
    /* DMA uses a large amount of memory that will never be freed */
    dma_addr = ut_steal_mem(DMA_SIZE_BITS);
    conditional_panic(dma_addr == 0, "Failed to reserve DMA memory\n");

    /* find available memory */
    ut_find_memory(&low, &high);

    /* Initialise the untyped memory allocator */
    ut_allocator_init(low, high);

    /* Initialise the cspace manager */
    err = cspace_root_task_bootstrap(ut_alloc, ut_free, ut_translate,
                                     malloc, free);
    conditional_panic(err, "Failed to initialise the c space\n");

    /* Initialise DMA memory */
    err = dma_init(dma_addr, DMA_SIZE_BITS);
    conditional_panic(err, "Failed to intiialise DMA memory\n");

    /* Initialise frametable */
    frametable_init();

    /* Initialiase other system compenents here */
    pid_init();

    /* Finally, setup IPC */
    _sos_ipc_init(ipc_ep, async_ep);
}

/*
 * Main entry point - called by crt.
 */
int main(void) {
    int ret;

    dprintf(0, "\nSOS Starting...\n");

    _sos_init(&_sos_ipc_ep_cap, &_sos_interrupt_ep_cap);

    /* Initialise the network hardware */
    network_init(_sos_interrupt_ep_cap);

    /* Initialise serial driver */ 
    ser_device = serial_init(); 
    conditional_panic(!ser_device, "Failed to initialise serial device\n"); 

    /* Initialise timers */
    seL4_CPtr timer_cap;
    timer_cap = cspace_mint_cap(cur_cspace, cur_cspace,
                    _sos_interrupt_ep_cap,
                    seL4_AllRights, seL4_CapData_Badge_new(IPC_TIMER_BADGE));

    ret = start_timer(timer_cap);
    conditional_panic(ret != CLOCK_R_OK, "Failed to initialise timer\n");

    /* Start all applications linked in the archive */
    for (int i = 0;; i++) {
        unsigned long size;
        char *name;
        void *data;

        data = cpio_get_entry (_cpio_archive, i, &name, &size);
        if (data != NULL) {
            printf ("trying to start %s...\n", name);
            pid_t pid = thread_create (name, 0, _sos_ipc_ep_cap);
            printf ("started with PID %d\n", pid);
        } else {
            break;
        }
    }

    seL4_TCB_Resume (threadlist_lookup(0)->tcb_cap);

    /* Wait on synchronous endpoint for IPC */
    dprintf(0, "\nSOS entering syscall loop\n");
    syscall_loop(_sos_ipc_ep_cap);

    /* Not reached */
    return 0;
}


