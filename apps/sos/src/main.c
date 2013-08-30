#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include <string.h>

#include <cspace/cspace.h>

#include <cpio/cpio.h>
#include <nfs/nfs.h>
#include <elf/elf.h>
#include <serial/serial.h>
#include <clock/clock.h>
#include <syscalls.h>

#include "network.h"
#include "elf.h"

#include "ut_manager/ut.h"
#include "vmem_layout.h"
#include "mapping.h"

#include "frametable.h"
#include "addrspace.h"

#include "thread.h"

#include <autoconf.h>

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>


#define USER_EP_CAP       (1)

#define TTY_NAME             CONFIG_SOS_STARTUP_APP
#define TTY_PRIORITY         (0)
#define TTY_EP_BADGE         (101)
#define IPC_TIMER_BADGE      (102)

#define PAGE_SIZE           (1 << seL4_PageBits)

/* The linker will link this symbol to the start address  *
 * of an archive of attached applications.                */
extern char _cpio_archive[];

const seL4_BootInfo* _boot_info;

seL4_CPtr _sos_ipc_ep_cap;
seL4_CPtr _sos_interrupt_ep_cap;

struct thread initial_process;

struct serial* ser_device;

/**
 * NFS mount point
 */
extern fhandle_t mnt_point;


void handle_syscall(seL4_Word badge, int num_args) {
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

        vaddr_t new_addr = as_resize_heap (initial_process.as, seL4_GetMR (1));

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

        switch (seL4_MessageInfo_get_label(message)) {
        case seL4_NoFault:
            handle_syscall(badge, seL4_MessageInfo_get_length(message) - 1);
            break;

        case seL4_VMFault:
            if (!seL4_GetMR(2)) {
                /* data fault; try to map in page */
                if (as_map_page (initial_process.as, seL4_GetMR(1))) {
                    /* restart calling thread now we have the page set */
                    seL4_Reply (message);
                    break;
                }
            }

            dprintf(0, "vm fault at 0x%08x, pc = 0x%08x, %s\n", seL4_GetMR(1),
                    seL4_GetMR(0),
                    seL4_GetMR(2) ? "Instruction Fault" : "Data fault");

            assert(!"Unable to handle vm faults");
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

void start_first_process(char* app_name, seL4_CPtr fault_ep) {
    int err;
    seL4_CPtr user_ep_cap;

    /* These required for setting up the TCB */
    seL4_UserContext context;

    /* These required for loading program sections */
    char* elf_base;
    unsigned long elf_size;

    /* Create a simple 1 level CSpace */
    initial_process.croot = cspace_create(1);
    assert(initial_process.croot != NULL);

    /* Copy the fault endpoint to the user app to enable IPC */
    user_ep_cap = cspace_mint_cap(initial_process.croot,
                                  cur_cspace,
                                  fault_ep,
                                  seL4_AllRights, seL4_CapData_Badge_new(TTY_EP_BADGE)
                                  );
    /* should be the first slot in the space, hack I know */
    assert(user_ep_cap == 1);
    assert(user_ep_cap == USER_EP_CAP);

    /* Create a new TCB object */
    initial_process.tcb_addr = ut_alloc(seL4_TCBBits);
    conditional_panic(!initial_process.tcb_addr, "No memory for new TCB");
    err =  cspace_ut_retype_addr(initial_process.tcb_addr,
                                 seL4_TCBObject,
                                 seL4_TCBBits,
                                 cur_cspace,
                                 &initial_process.tcb_cap);
    conditional_panic(err, "Failed to create TCB");

    /* create address space for process */
    initial_process.as = addrspace_create (0);
    conditional_panic(!initial_process.as, "failed to create process address space");

    /* Map in IPC first off (since we need it for TCB configuration) */
    as_define_region (initial_process.as, PROCESS_IPC_BUFFER, PAGE_SIZE, seL4_AllRights, REGION_IPC);
    if (!as_map_page (initial_process.as, PROCESS_IPC_BUFFER)) {
        panic ("could not map IPC buffer");
    }

    seL4_CPtr ipc_cap = as_get_page_cap (initial_process.as, PROCESS_IPC_BUFFER);
    conditional_panic (!ipc_cap, "could not fetch IPC cap back just after map");

    /* Configure the TCB */
    err = seL4_TCB_Configure(initial_process.tcb_cap, user_ep_cap, TTY_PRIORITY,
                             initial_process.croot->root_cnode, seL4_NilData,
                             initial_process.as->pagedir_cap, seL4_NilData, PROCESS_IPC_BUFFER,
                             ipc_cap);
    conditional_panic(err, "Unable to configure new TCB");

    /* parse the dite image */
    dprintf(1, "\nStarting \"%s\"...\n", app_name);
    elf_base = cpio_get_file(_cpio_archive, app_name, &elf_size);
    conditional_panic(!elf_base, "Unable to locate cpio header");

    /* load the elf image */
    err = elf_load(initial_process.as, elf_base);
    conditional_panic(err, "Failed to load elf image");

    /* find where we put the stack */
    struct as_region* stack = as_get_region_by_type (initial_process.as, REGION_STACK);
    vaddr_t stack_top = stack->vbase + stack->size;
    printf ("stack top = 0x%x\n", stack_top);
    printf ("stack base = 0x%x\n", stack->vbase);

    /* Start the new process */
    memset(&context, 0, sizeof(context));
    context.pc = elf_getEntryPoint(elf_base);
    context.sp = stack_top;
    seL4_TCB_WriteRegisters(initial_process.tcb_cap, 1, 0, 2, &context);
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

    /* Start the user application */
    start_first_process(TTY_NAME, _sos_ipc_ep_cap);

    /* Initialise timers */
    seL4_CPtr timer_cap;
    timer_cap = cspace_mint_cap(cur_cspace,
                    cur_cspace,
                    _sos_interrupt_ep_cap,
                    seL4_AllRights, seL4_CapData_Badge_new(IPC_TIMER_BADGE)
                    );

    ret = start_timer(timer_cap);
    conditional_panic(ret != CLOCK_R_OK, "Failed to initialise timer\n");

    /* Wait on synchronous endpoint for IPC */
    dprintf(0, "\nSOS entering syscall loop\n");
    syscall_loop(_sos_ipc_ep_cap);

    /* Not reached */
    return 0;
}


