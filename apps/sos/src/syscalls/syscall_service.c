#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>

#include <string.h>
#include <stdio.h>
#include "mapping.h"
#include "vm/vmem_layout.h"
#include <pawpaw.h>
#include <copyinout.h>

#include <assert.h>

#define MAX_SERVICE_NAME    512

extern thread_t current_thread;

/* FIXME: should have per-thread limit on # of waiting svcs */
struct svc_wait {
    char* name;
    struct pawpaw_event *src_evt;
    thread_t thread;

    struct svc_wait* next;
};

struct svc_wait* svc_waitlist = NULL;

int syscall_service_register (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    /* copy the thread's "service" endpoint cap into our cspace so we can dish it out */
    seL4_Word our_cap = cspace_copy_cap (cur_cspace, current_thread->croot, evt->args[0], seL4_AllRights);

    if (our_cap) {
        current_thread->service_cap = our_cap;
    }

    /* check if we're registering a service and someone was waiting on us already */
    struct svc_wait* sw = svc_waitlist;
    struct svc_wait* prev = NULL;
    while (sw) {
        int delete = false;
        if (strcmp (current_thread->name, sw->name) == 0) {
            /* found it, wake it and remove */
            seL4_CPtr client_cap = cspace_mint_cap(sw->thread->croot, cur_cspace, current_thread->service_cap,
                seL4_AllRights, seL4_CapData_Badge_new (sw->thread->pid));

            seL4_SetMR (0, client_cap);
            seL4_Send (sw->src_evt->reply_cap, sw->src_evt->reply);
            pawpaw_event_dispose (sw->src_evt);

            delete = true;
        }

        struct svc_wait* next = sw->next;

        /* remove in place if required */
        if (delete) {
            if (prev) {
                prev->next = next;
            } else {
                svc_waitlist = next;
            }

            free (sw->name);
            free (sw);
        } else {
            /* only update prev if we're not deleting */
            prev = sw;
        }

        sw = next;
    }

    seL4_SetMR (0, our_cap ? 0 : 1);

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_service_find (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    char service_name[MAX_SERVICE_NAME] = {0};
    if (!copyin (current_thread, evt->args[0], evt->args[1], service_name, MAX_SERVICE_NAME)) {
        /* invalid request, ignore */
        return PAWPAW_EVENT_UNHANDLED;
    }

    printf ("%s: looking for svc %s\n", __FUNCTION__, service_name);

    thread_t found_thread = NULL;
    thread_t check_thread = threadlist_first();
    while (check_thread) {
        /* FIXME: shouldn't be comparing on thread name, rather name registered with */
        if (strcmp (check_thread->name, service_name) == 0) {
            found_thread = check_thread;
            break;
        }

        check_thread = check_thread->next;
    }

    if (found_thread && found_thread->service_cap) {
        printf ("found a thread %s with that service\n", found_thread->name);
        seL4_CPtr client_cap = cspace_mint_cap(current_thread->croot, cur_cspace, found_thread->service_cap,
            seL4_AllRights, seL4_CapData_Badge_new (current_thread->pid));

        seL4_SetMR (0, client_cap);
    } else if (seL4_GetMR (1) > 0) {
        /* if service asked to block, wait for it to come available */
        struct svc_wait* sw = malloc (sizeof (struct svc_wait));

        sw->name = strdup (service_name);
        sw->src_evt = evt;
        sw->thread = current_thread;

        /* push onto waitlist stack */
        sw->next = svc_waitlist;
        svc_waitlist = sw;

        return PAWPAW_EVENT_HANDLED_SAVED;
    } else {
        /* not available yet */
        seL4_SetMR (0, 0);
    }
    
    return PAWPAW_EVENT_NEEDS_REPLY;
}