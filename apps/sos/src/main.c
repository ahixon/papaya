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

extern char _cpio_archive[];        /* FIXME: move this out of here, one day.. */

const seL4_BootInfo* _boot_info;
seL4_CPtr _sos_ipc_ep_cap;
extern seL4_CPtr _badgemap_ep;

void syscall_loop (seL4_CPtr ep) {
    while (1) {
        seL4_Word badge;
        seL4_MessageInfo_t message;

        message = seL4_Wait (ep, &badge);

        /* look for the thread associated with the syscall */
        thread_t thread = threadlist_lookup (badge);
        if (!thread && badge > 0) {
            printf ("syscall: invalid thread - had badge %d\n", badge);
            continue;
        }

        switch (seL4_MessageInfo_get_label (message)) {
        case seL4_NoFault:
        {
            unsigned int argc = seL4_MessageInfo_get_length (message);
            if (argc < 1) {
                printf ("syscall: thread %s provided no syscall ID, ignoring\n", thread->name);
                break;
            }

            /* get syscall info from our jumptable */
            unsigned int syscall_id = seL4_GetMR (0);
            if (syscall_id >= NUM_SYSCALLS) {
                printf ("syscall: thread %s provided invalid syscall ID\n", thread->name);
                break;
            }

            struct syscall_info sc = syscall_table[syscall_id];
            argc--; /* since we don't want syscall ID as an arg to the func */

            /* ensure argument count is OK */
            if (argc != sc.argcount) {
                printf ("syscall: syscall for %u had arg count %u but required %u\n", syscall_id, argc, sc.argcount);
                break;
            }

            if (sc.reply) {
                /* Save the caller */
                seL4_CPtr reply_cap = cspace_save_reply_cap(cur_cspace);

                /* finally, call the func pointer */
                seL4_MessageInfo_t reply = sc.scall_func (thread);

                /* and reply */
                seL4_Send (reply_cap, reply);
                cspace_free_slot(cur_cspace, reply_cap);
            } else {
                sc.scall_func (thread);
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

            /*dprintf(0, "vm fault at 0x%08x, pc = 0x%08x, %s\n", seL4_GetMR(1),
                    seL4_GetMR(0),
                    seL4_GetMR(2) ? "Instruction Fault" : "Data fault");*/
            printf ("non-recoverable VM fault; not waking thread %d (%s)...\n", thread->pid, thread->name);
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

/*
 * Initialisation of subsystems and resources required for Papaya.
 */
static void _sos_init(seL4_CPtr* ipc_ep){
    seL4_Word dma_addr;
    seL4_Word low, high;
    int err;

    /* Retrieve boot info from seL4 */
    _boot_info = seL4_GetBootInfo();
    conditional_panic(!_boot_info, "Failed to retrieve boot info\n");
    if(verbose > 0){
        //print_bootinfo(_boot_info);
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
    #if 0
    err = dma_init(dma_addr, DMA_SIZE_BITS);
    conditional_panic(err, "Failed to intiialise DMA memory\n");
    #endif

    /* Initialise frametable */
    frametable_init();

    /* Initialiase other system compenents here */
    uid_init();

    /* Finally, create an endpoint for user application IPC */
    seL4_Word ep_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!ep_addr, "No memory for endpoint");
    err = cspace_ut_retype_addr(ep_addr, 
                                seL4_EndpointObject,
                                seL4_EndpointBits,
                                cur_cspace,
                                ipc_ep);
    conditional_panic(err, "Failed to allocate c-slot for IPC endpoint");

    /* create the thing thing */
    printf ("creating badgemap...\n");
    /*thread_t badgethread = */thread_create_at ("badgemap", mapper_main, _sos_ipc_ep_cap);

    //seL4_Word bm_ep;
    ep_addr = ut_alloc(seL4_EndpointBits);
    conditional_panic(!ep_addr, "No memory for endpoint");
    err = cspace_ut_retype_addr(ep_addr, 
                                seL4_EndpointObject,
                                seL4_EndpointBits,
                                /*badgethread->croot,*/
                                cur_cspace,
                                &_badgemap_ep);
    conditional_panic(err, "Failed to allocate c-slot for badgemap");

     //= cspace_copy_cap (badgethread->croot, cur_cspace, bm_ep, seL4_AllRights);

    printf ("done\n");
}

/*
 * Main entry point - called by crt.
 */
int main (void) {
    dprintf (0, "\nPapaya starting...\n");

    /* initialise root server from whatever seL4 left us */
    _sos_init (&_sos_ipc_ep_cap);

    /* boot up core services */
    thread_create ("svc_dev", _sos_ipc_ep_cap);
    thread_create ("svc_vfs", _sos_ipc_ep_cap);
    // thread_create ("svc_net", _sos_ipc_ep_cap);
    // FIXME: need to rename svc_network -> svc_net

    /* boot up device filesystem & mount it */
    //thread_create ("fs_dev", _sos_ipc_ep_cap);
    // FIXME: actually mount the thing

    /* start any devices services inside the CPIO archive */
    dprintf (1, "Looking for device services linked into CPIO...\n");
    unsigned long size;
    char *name;
    for (int i = 0; cpio_get_entry (_cpio_archive, i, (const char**)&name, &size); i++) {
        if (strstr (name, "dev_") == name) {
            thread_create (name, _sos_ipc_ep_cap);
        }
    }

    /* finally, start the boot app */
    dprintf (1, "Starting boot application \"%s\"...\n", CONFIG_SOS_STARTUP_APP);
    pid_t pid = thread_create (CONFIG_SOS_STARTUP_APP, _sos_ipc_ep_cap);
    dprintf (1, "  started with PID %d\n", pid);

    /* and wait for IPC */
    dprintf (0, "SOS entering syscall loop...\n");
    syscall_loop(_sos_ipc_ep_cap);

    return 0;   /* not reached */
}