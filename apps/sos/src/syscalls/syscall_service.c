#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>

#include <string.h>
#include <stdio.h>
#include "mapping.h"
#include "vm/vmem_layout.h"

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
    if (err) {
        return NULL;
    }

    last_cap = cap;

    return (void*)(FRAMEWINDOW_VSTART + page_offset);
}

void uspace_unmap (void* addr) {
    /* FIXME: no seriously what the fuck is this */
    seL4_ARM_Page_Unmap (last_cap);
}

seL4_MessageInfo_t syscall_service_register (thread_t thread) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);

    seL4_Word our_cap = cspace_copy_cap (cur_cspace, thread->croot, seL4_GetMR (1), seL4_AllRights);

    if (our_cap) {
        thread->service_cap = our_cap;
    }

    seL4_SetMR (0, our_cap ? 0 : 1);
    return reply;
}

seL4_MessageInfo_t syscall_service_find (thread_t thread) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);

    char* service_name = uspace_map (thread->as, (vaddr_t)seL4_GetMR(1));
    service_name[256] = '\0';

    thread_t found_thread = NULL;

    printf ("%s asked for service name %s\n", thread->name, service_name);

    thread_t check_thread = threadlist_first();
    while (check_thread) {
        /* FIXME: shouldn't be comparing on thread name, rather name registered with */
        printf ("comparing %s and %s\n", check_thread->name, service_name);
        if (strcmp (check_thread->name, service_name) == 0) {
            found_thread = check_thread;
            break;
        }

        check_thread = check_thread->next;
    }

    if (found_thread && found_thread->service_cap) {
        printf ("Found service on thread %s! badging with %d\n", found_thread->name, thread->pid);

        seL4_CPtr client_cap = cspace_mint_cap(thread->croot, cur_cspace, found_thread->service_cap,
            seL4_AllRights, seL4_CapData_Badge_new (thread->pid));

        struct req_svc* requested = malloc (sizeof (struct req_svc));
        if (!requested) {
            seL4_SetMR (0, 0);
            uspace_unmap (service_name);
            return reply;
        }

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
    } else {
        seL4_SetMR (0, 0);
    }

    uspace_unmap (service_name);
    return reply;
}