#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>

#include <string.h>
#include <stdio.h>
#include "mapping.h"
#include "vm/vmem_layout.h"

#include <assert.h>

seL4_CPtr last_cap = 0;
seL4_CPtr last_cap_next = 0;
seL4_CPtr current_reply_cap = 0;    /* FIXME: hack so it compiles THIS NEEDS TO GO (in evt anyway) */

void* uspace_map (addrspace_t as, vaddr_t vaddr) {
    assert (!last_cap);

    /* BIG FIXME: this function makes many assumptions that DEFINITELY need to be fixed:
        * assumes that does not cross page boundary.
        * assumes is in valid region (including crossing over to invalid region)
    */
    for (int i = 0; i < 2; i++) {
        seL4_CPtr cap = as_get_page_cap (as, vaddr + (i * (1<< seL4_PageBits)));
        if (!cap && last_cap == 0) {
            printf ("NO CAP???\n");
            return NULL;
        } else {
            // #yolo
            cap = last_cap;
        }

        int err = map_page(cap, seL4_CapInitThreadPD, FRAMEWINDOW_VSTART + (i * (1<<seL4_PageBits)), seL4_AllRights, seL4_ARM_Default_VMAttributes);
        if (err) {
            printf ("page map failed\n");
            return NULL;
        }

        if (last_cap == 0) {
            last_cap = cap;
        } else {
            last_cap_next = cap;
        }
    }

    //printf ("** page offset = 0x%x, vaddr was 0x%x\n", page_offset, vaddr);
    vaddr_t page_offset = vaddr % (1 << seL4_PageBits);
    return (void*)(FRAMEWINDOW_VSTART + page_offset);
}

void uspace_unmap (void* addr) {
    /* FIXME: no seriously what the fuck is this */
    seL4_ARM_Page_Unmap (last_cap);
    if (last_cap_next) {
        seL4_ARM_Page_Unmap (last_cap_next);
    }
    last_cap = 0;
    last_cap_next = 0;
}

/* FIXME: should have per-thread limit on # of waiting svcs */
struct svc_wait {
    char* name;
    seL4_CPtr reply_cap;
    thread_t thread;

    struct svc_wait* next;
};

struct svc_wait* svc_waitlist = NULL;

seL4_MessageInfo_t syscall_service_register (thread_t thread) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);

    seL4_Word our_cap = cspace_copy_cap (cur_cspace, thread->croot, seL4_GetMR (1), seL4_AllRights);

    if (our_cap) {
        thread->service_cap = our_cap;
    }

    struct svc_wait* sw = svc_waitlist;
    while (sw) {
        printf ("[POST] comparing %s and %s\n", thread->name, sw->name);
        if (strcmp (thread->name, sw->name) == 0) {
            /* found it, wake it and remove */
            seL4_CPtr client_cap = cspace_mint_cap(sw->thread->croot, cur_cspace, thread->service_cap,
                seL4_AllRights, seL4_CapData_Badge_new (sw->thread->pid));

            seL4_SetMR (0, client_cap);
            seL4_Send (sw->reply_cap, reply);

            /* FIXME: remove in place */
        }

        sw = sw->next;
    }

    seL4_SetMR (0, our_cap ? 0 : 1);
    return reply;
}

seL4_MessageInfo_t syscall_service_find (thread_t thread) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);

    
    char* service_name = uspace_map (thread->as, (vaddr_t)seL4_GetMR(1));
    if (!service_name) {
        printf ("map failed on vaddr 0x%x for thread %s\n", seL4_GetMR (1), thread->name);
        uspace_unmap (service_name);
        seL4_SetMR (0, 0);
        return reply;
    }

    printf ("HAD SERVICE from %s? %p %s\n", thread->name, service_name, service_name);
    //service_name[128] = '\0';

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

            seL4_Send (current_reply_cap, reply);
            cspace_free_slot(cur_cspace, current_reply_cap);

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
    } else if (seL4_GetMR (1) > 0) {
        struct svc_wait* sw = malloc (sizeof (struct svc_wait));
        sw->name = malloc (strlen (service_name));
        strcpy (sw->name, service_name);

        sw->reply_cap = current_reply_cap;
        sw->thread = thread;

        sw->next = svc_waitlist;
        svc_waitlist = sw;

        uspace_unmap (service_name);
        return reply;
    } else {
        seL4_SetMR (0, 0);
    }

    uspace_unmap (service_name);
    
    /* send the guy */
    seL4_Send (current_reply_cap, reply);
    cspace_free_slot(cur_cspace, current_reply_cap);
    return reply;
}