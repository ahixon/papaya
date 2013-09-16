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
#include <pawpaw.h>

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

const seL4_BootInfo* _boot_info;
extern char _cpio_archive[];

seL4_CPtr _sos_ipc_ep_cap;

#define MAX_SERVICE_LENGTH  64
#define SVC_NAME_TOO_LONG       2
#define SVC_OK                  0
#define SVC_NOT_FOUND           404

/**
 * NFS mount point
 */
extern fhandle_t mnt_point;

seL4_CPtr last_cap;

void* uspace_map (addrspace_t as, vaddr_t vaddr) {
    /* BIG FIXME: this function makes many assumptions that DEFINITELY need to be fixed:
        * assumes that does not cross page boundary.
        * assumes is in valid region (including crossing over to invalid region)
    */
    seL4_CPtr cap = as_get_page_cap (as, vaddr);
    if (!cap) {
        printf ("NO CAP???\n");
        return NULL;
    }

    // FIXME: yuck!!
    vaddr_t page_offset = vaddr % (1 << seL4_PageBits);

    int err = map_page(cap, seL4_CapInitThreadPD, FRAMEWINDOW_VSTART, seL4_AllRights, seL4_ARM_Default_VMAttributes);
    conditional_panic(err, "could not map into SOS");

    last_cap = cap;

    return (void*)(FRAMEWINDOW_VSTART + page_offset);
}

void uspace_unmap (void* addr) {
    /* FIXME: no seriously what the fuck is this */
    seL4_ARM_Page_Unmap (last_cap);
}


void handle_syscall(thread_t thread, int num_args) {
    seL4_Word syscall_number;
    seL4_CPtr reply_cap;
    seL4_MessageInfo_t reply;

    seL4_CPtr our_cap;


    syscall_number = seL4_GetMR(0);

    /* Save the caller */
    reply_cap = cspace_save_reply_cap(cur_cspace);
    assert(reply_cap != CSPACE_NULL);

    /* Process system call */
    switch (syscall_number) {
#if 0
    case SYSCALL_NETWRITE:
        printf ("syscall: asked to write over network serial\n");
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
#endif
    case SYSCALL_SBRK:
        if (num_args < 0) {
            break;
        }

        //printf ("syscall: asked for sbrk\n");
        vaddr_t new_addr = as_resize_heap (thread->as, seL4_GetMR (1));

        reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR(0, new_addr);
        seL4_Send(reply_cap, reply);

        cspace_free_slot(cur_cspace, reply_cap);
        break;

    /* could be register driver? */
    case SYSCALL_REGISTER_IRQ:
        if (num_args != 1) {
            break;
        }

        printf ("syscall: %s asked to register IRQ %d\n", thread->name, seL4_GetMR(1));
        /* FIXME: probably want to copy instead, so that we can revoke later if needed */
        seL4_CPtr irq_cap = cspace_irq_control_get_cap(cur_cspace, seL4_CapIRQControl, seL4_GetMR(1));
        conditional_panic (!irq_cap, "NO IRQ CAP??");

        printf ("IRQ cap now = %d\n", irq_cap);

        seL4_CPtr their_cap = cspace_copy_cap (thread->croot, cur_cspace, irq_cap, seL4_AllRights);
        printf ("their IRQ = %d\n", their_cap);

        reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR (0, their_cap);      // OK
        //seL4_SetCap(0, their_cap);
        seL4_Send(reply_cap, reply);

        cspace_free_slot(cur_cspace, reply_cap);
        break;

    case SYSCALL_MAP_DEVICE:
        if (num_args != 2) {
            break;
        }

        reply = seL4_MessageInfo_new(0, 0, 0, 1);
        seL4_SetMR (0, (seL4_Word)map_device_thread ((void*)seL4_GetMR(1), seL4_GetMR(2), thread));
        seL4_Send (reply_cap, reply);
        cspace_free_slot(cur_cspace, reply_cap);
        break;        

    case SYSCALL_ALLOC_CNODES:
        if (num_args != 1) {
            break;
        }

        reply = seL4_MessageInfo_new(0, 0, 0, 2);
        seL4_CPtr root_cptr;

        seL4_SetMR (1, thread_cspace_new_cnodes (thread, seL4_GetMR (1), &root_cptr));
        seL4_SetMR (0, root_cptr);

        seL4_Send (reply_cap, reply);
        cspace_free_slot(cur_cspace, reply_cap);
        break;

    case SYSCALL_CREATE_EP_SYNC:
        reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, thread_cspace_new_ep (thread));

        seL4_Send (reply_cap, reply);
        cspace_free_slot(cur_cspace, reply_cap);
        break;

    case SYSCALL_CREATE_EP_ASYNC:
        reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, thread_cspace_new_async_ep (thread));

        seL4_Send (reply_cap, reply);
        cspace_free_slot(cur_cspace, reply_cap);
        break;

    case SYSCALL_BIND_AEP_TCB:
        if (num_args != 1) {
            break;
        }

        reply = seL4_MessageInfo_new (0, 0, 0, 1);

        our_cap = cspace_copy_cap (cur_cspace, thread->croot, seL4_GetMR (1), seL4_AllRights);;
        assert (our_cap);

        printf ("want to bind 0x%x (from 0x%x) and 0x%x\n", our_cap, seL4_GetMR(1), thread->tcb_cap);

        seL4_SetMR (0, seL4_TCB_BindAEP (thread->tcb_cap, our_cap));

        seL4_Send (reply_cap, reply);
        cspace_free_slot(cur_cspace, reply_cap);
        break;

    case SYSCALL_SUICIDE:
        /* FIXME: should actually destroy thread + resources instead of not returning */
        printf ("\n!!! thread %s wanted to die - R U OK? !!!\n", thread->name);
        break;

    case SYSCALL_CAN_NEGOTIATE:
        if (num_args != 3) {
            break;
        }

        /* ok, first lookup who the service was */
        seL4_CPtr client_cap = seL4_GetMR (1);
        struct req_svc* svcinfo = thread->known_services;
        while (svcinfo) {
            if (svcinfo->cap == client_cap) {
                break;
            }

            svcinfo = svcinfo->next;
        }

        if (!svcinfo) {
            printf ("canneg: failed to find service with your cap %d\n", client_cap);
            reply = seL4_MessageInfo_new (0, 0, 0, 1);
            seL4_SetMR (0, 0);
            seL4_Send (reply_cap, reply);
            break;
        }

        unsigned int a_proposes = seL4_GetMR (2);
        vaddr_t can_vaddr_a = seL4_GetMR (3);

        /* cool, have the other thread; ask them what they think */
        /* FIXME: should be a seL4_Send, and wait for their reply in the main event loop, otherwise
         *        they can hold up the root server by NEVER REPLYING then NOBODY CAN DO SHIT - this is bad! :( */

        /* look up cap from thread rather than svcinfo since then we will have no badge and thus can verify
         * is truly from the root server not another client trying to be tricky */
        printf ("asking other thread %s what size can they want\n", svcinfo->svc_thread->name);
        seL4_MessageInfo_t query_msg = seL4_MessageInfo_new (0, 0, 0, 3);
        seL4_SetMR (0, SYSCALL_CAN_NEGOTIATE);
        seL4_SetMR (1, a_proposes);
        seL4_SetMR (2, thread->pid);

        seL4_Call (svcinfo->svc_thread->service_cap, query_msg);

        unsigned int b_proposes = seL4_GetMR (0);

        printf ("OK B proposes %d, A proposes %d\n", b_proposes, a_proposes);

        unsigned int min = a_proposes;
        if (b_proposes < a_proposes) {
            min = b_proposes;
        }

        printf ("settled on size %u\n", min);

        /* require at least 2 pages so can do sequential operations */
        /* FIXME: function-ise this! */
        if (min >= 2) {
            /* do A first */
            vaddr_t start_a;
            //struct as_region* bean_reg_a = as_get_region_by_type (thread->as, REGION_BEANS);
            //if (!bean_reg_a) {
                printf ("creating new beans area for A\n");
                /* create a bean region */
                struct as_region* bean_reg_a = as_define_region (thread->as, PROCESS_BEANS, PAGE_SIZE * min, seL4_AllRights, REGION_BEANS);
                start_a = bean_reg_a->vbase;
            //} else {
                //printf ("had existing region for beans\n");
                /* extend the bean region */
                //start_a = bean_reg_a->vbase;
                //bean_reg_a = as_resize_region (thread->as, bean_reg_a, PAGE_SIZE * min);
            //}

            printf ("bean_reg_a = %p\n", bean_reg_a);
            assert (bean_reg_a);

            /* ok update their structure */
            printf ("had canA vaddr = 0x%x\n", can_vaddr_a);
            struct pawpaw_can* can_a = uspace_map (thread->as, can_vaddr_a);
            assert (can_a);

            can_a->start = (void*)start_a;
            can_a->count = min;
            can_a->uid = 0;
            can_a->last_bean = -1;

            uspace_unmap (can_a);

            /* and B */
            vaddr_t start_b;
            vaddr_t offset_b = PROCESS_BEANS;

            struct as_region* bean_reg_b = as_get_region_by_type (svcinfo->svc_thread->as, REGION_BEANS);
            /* FIXME: what about fragmentation! */
            if (bean_reg_b) {
                offset_b = bean_reg_b->vbase + bean_reg_b->size;
            }

            /* create a bean region */
            bean_reg_b = as_define_region (svcinfo->svc_thread->as, offset_b, PAGE_SIZE * min, seL4_AllRights, REGION_BEANS);
            start_b = bean_reg_b->vbase;

            assert (bean_reg_b);

            /* ok update their structure */
            vaddr_t can_vaddr_b = seL4_GetMR (1);
            printf ("had canB vaddr = 0x%x\n", can_vaddr_b);
            struct pawpaw_can* can_b = uspace_map (svcinfo->svc_thread->as, can_vaddr_b);
            assert (can_b);

            can_b->start = (void*)start_b;
            can_b->count = min;
            can_b->uid = thread->pid;
            can_b->last_bean = -1;

            uspace_unmap (can_b);
        

            /* link beans */
            int success = as_region_link (bean_reg_a, bean_reg_b);
            assert (success);
        }

        /* bye */
        printf ("sending reply\n");
        reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, 0);
        seL4_Send (reply_cap, reply);

        break;

    case SYSCALL_REGISTER_SERVICE:
        if (num_args != 1) {
            break;
        }

        reply = seL4_MessageInfo_new (0, 0, 0, 1);

        our_cap = cspace_copy_cap (cur_cspace, thread->croot, seL4_GetMR (1), seL4_AllRights);

        if (our_cap) {
            thread->service_cap = our_cap;
        }

        seL4_SetMR (0, our_cap ? 0: 1);
        seL4_Send (reply_cap, reply);

        cspace_free_slot(cur_cspace, reply_cap);
        break;

    case SYSCALL_FIND_SERVICE:
        if (num_args != 2) {
            break;
        }

        printf ("syscall: %s asked to find a service\n", thread->name);
        reply = seL4_MessageInfo_new(0, 0, 0, 1);

        if (seL4_GetMR (2) >= MAX_SERVICE_LENGTH) {
            seL4_SetMR(0, 0);
        } else {
            /* assume there's only one service at the moment */
            // FIXME: copyin would be nicer, but since we're only using the string as read-only it's
            // probably OK
            pid_t initial_pid = thread->pid;

            char* service_name = uspace_map (thread->as, (vaddr_t)seL4_GetMR(1));
            int found = false;
            thread_t found_thread = NULL;

            printf ("asked for service name %s\n", service_name);

            if (strcmp (service_name, "sys.net.services") == 0) {
                thread_t thread = threadlist_first();
                while (thread) {
                    //printf ("comparing %s to svc_network\n", thread->name);
                    if (strcmp (thread->name, "svc_network") == 0) {
                        found_thread = thread;
                        found = true;
                        break;
                    }

                    thread = thread->next;
                }
            }

            if (strcmp (service_name, "sys.vfs") == 0) {
                thread_t thread = threadlist_first();
                while (thread) {
                    printf ("comparing %s to svc_vfs\n", thread->name);
                    if (strcmp (thread->name, "svc_vfs") == 0) {
                        found_thread = thread;
                        found = true;
                        break;
                    }

                    thread = thread->next;
                }
            }

            if (strcmp (service_name, "dev.timer") == 0) {
                thread_t thread = threadlist_first();
                while (thread) {
                    //printf ("comparing %s to svc_network\n", thread->name);
                    if (strcmp (thread->name, "dev_timer") == 0) {
                        found_thread = thread;
                        found = true;
                        break;
                    }

                    thread = thread->next;
                }
            }

            if (strcmp (service_name, "sys.dev") == 0) {
                thread_t thread = threadlist_first();
                while (thread) {
                    //printf ("comparing %s to svc_network\n", thread->name);
                    if (strcmp (thread->name, "svc_dev") == 0) {
                        found_thread = thread;
                        found = true;
                        break;
                    }

                    thread = thread->next;
                }
            }

            if (found && found_thread->service_cap) {
                printf ("Found service on thread %s! badging with %d\n", found_thread->name, thread->pid);

                /* NOTE: NO GRANT PERMISSIONS */
                seL4_CPtr client_cap = cspace_mint_cap(thread->croot, cur_cspace, found_thread->service_cap,
                    seL4_AllRights /*seL4_CanRead | seL4_CanWrite*/, seL4_CapData_Badge_new (initial_pid));

                struct req_svc* requested = malloc (sizeof (struct req_svc));
                assert (requested);

                requested->svc_thread = found_thread;
                requested->cap = client_cap;
                requested->next = NULL;

                /* add to linked list */
                if (!thread->known_services) {
                    thread->known_services = requested;                    
                } else {
                    thread->known_services->next = requested;
                }

                seL4_SetMR (0, client_cap);

                uspace_unmap (service_name);
                goto sendServiceReply;
            }

            printf ("Service not found!\n");
            seL4_SetMR (0, 0);
            uspace_unmap (service_name);
        }

sendServiceReply:
        seL4_Send (reply_cap, reply);
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
        seL4_MessageInfo_t message;

        //printf ("Waiting on EP 0x%x\n", ep);
        message = seL4_Wait(ep, &badge);

        thread_t thread;

        switch (seL4_MessageInfo_get_label(message)) {
        case seL4_NoFault:
            thread = threadlist_lookup (badge);
            if (!thread) {
                printf ("syscall: invalid thread - had badge %d\n", badge);
                break;
            }

            //printf ("had syscall %d from %s\n", seL4_GetMR(0), thread->name);
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
            printf ("non-recoverable VM fault; not waking thread %d (%s)...\n", thread->pid, thread->name);

            //assert(!"Unable to handle vm faults");
            break;

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

static void _sos_ipc_init(seL4_CPtr* ipc_ep){
    seL4_Word ep_addr/*, aep_addr*/;
    int err;

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
    err = dma_init(dma_addr, DMA_SIZE_BITS);
    conditional_panic(err, "Failed to intiialise DMA memory\n");

    /* Initialise frametable */
    frametable_init();

    /* Initialiase other system compenents here */
    pid_init();

    /* Finally, setup IPC */
    _sos_ipc_init(ipc_ep);
}

/*
 * Main entry point - called by crt.
 */
int main (void) {
    dprintf (0, "\nSOS starting...\n");

    /* initialise root server from whatever seL4 left us */
    _sos_init (&_sos_ipc_ep_cap);

    /* boot up core services */
    thread_create ("svc_dev", _sos_ipc_ep_cap);
    thread_create ("svc_vfs", _sos_ipc_ep_cap);
    // thread_create ("svc_net", _sos_ipc_ep_cap);
    // FIXME: need to rename svc_network -> svc_net

    /* boot up device filesystem & mount it */
    thread_create ("fs_dev", _sos_ipc_ep_cap);
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