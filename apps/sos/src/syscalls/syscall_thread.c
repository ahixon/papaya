#include <pawpaw.h>
#include <sel4/sel4.h>
#include <thread.h>
#include <stdio.h>
#include <copyinout.h>
#include <sos.h>
#include <string.h>

#define THREAD_PATH_SIZE_MAX	512
#define MAX_PROCESS_LIST_SIZE   30

extern thread_t current_thread;
extern seL4_CPtr rootserver_syscall_cap;

void print_resource_stats (void);

int syscall_thread_suicide (struct pawpaw_event* evt) {
	printf ("thread %s asked to terminate\n", current_thread->name);
	thread_destroy (current_thread);

    return PAWPAW_EVENT_HANDLED;
}

void finish_thread_create (struct pawpaw_event *evt, struct frameinfo* frame) {
    char* path = (char*)evt->args[0];
    char* block = (char*)evt->args[1];

    if (frame) {
        thread_t child = thread_create_from_fs (path, block, evt->args[2], evt->args[3], evt->args[4]);
        if (child) {
            seL4_SetMR (0, child->pid);
        } else {
            seL4_SetMR (0, -1);
        }
    } else {
        seL4_SetMR (0, -1);
    }

    struct pawpaw_event* oldevt = (struct pawpaw_event*)evt->args[5];
    if (oldevt) {
        seL4_Send (oldevt->reply_cap, oldevt->reply);

        /* and free */
        pawpaw_event_dispose (oldevt);

        //return PAWPAW_EVENT_HANDLED;
        /* FIXME: free shared buf page */
    }
}

int syscall_thread_create (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    char thread_path[THREAD_PATH_SIZE_MAX] = {0};
    if (!copyin (current_thread, evt->args[0], evt->args[1], thread_path,
        THREAD_PATH_SIZE_MAX)) {

        /* invalid request, ignore */
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* FIXME: WELL, I BROKE THE NFS FILE LOADING BUT HAVE NO TIME TO FIX IT - OOPS */
    //thread_t child = thread_create_from_fs (thread_path, rootserver_syscall_cap);
    //thread_t child = thread_create_from_cpio (thread_path, rootserver_syscall_cap);
    thread_create_from_fs_oneblock (thread_path, rootserver_syscall_cap, finish_thread_create, evt);
    return PAWPAW_EVENT_HANDLED_SAVED;
}

int syscall_thread_destroy (struct pawpaw_event* evt) {
	thread_t target = thread_lookup (evt->args[0]);

	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

	if (target) {
		thread_destroy (target);
		seL4_SetMR (0, 0);
	} else {
		seL4_SetMR (0, -1);
	}

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_thread_pid (struct pawpaw_event* evt) {
	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
	seL4_SetMR (0, current_thread->pid);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_thread_wait (struct pawpaw_event* evt) {
    /* TODO: argument 0 can also be -1 to indicate any process
     * this requires a global bequests queue. also a pretty dumb syscall */
    thread_t target = thread_lookup (evt->args[0]);
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    if (!target) {
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    /* ok have target pid, register in it's "to notify" list and 
     * go back to event loop */

    struct pawpaw_saved_event *saved = malloc (sizeof (
        struct pawpaw_saved_event));

    if (!saved) {
        seL4_SetMR (0, -1);
        return PAWPAW_EVENT_NEEDS_REPLY;
    }

    saved->evt = evt;
    saved->next = target->bequests;
    target->bequests = saved;

    return PAWPAW_EVENT_HANDLED_SAVED;
}

int syscall_thread_list (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    vaddr_t dest = evt->args[0];
    unsigned count = evt->args[1];
    size_t usize = count * sizeof (process_t);

    if (count > MAX_PROCESS_LIST_SIZE) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    process_t* processes = malloc (usize);
    thread_t thread = threadlist_first ();
    int i = 0;
    while (thread) {
        processes[i].pid = thread->pid;

        /* this is just the VM size, not the physical size as per spec? */
        struct as_region* reg = thread->as->regions;
        while (reg) {
            processes[i].size += reg->size;
            reg = reg->next;
        }

        processes[i].stime = thread->start;
        strncpy (processes[i].command, thread->name, N_NAME);

        i++;
        thread = thread->next;
    }
    
    if (copyout (current_thread, dest, usize, (char*)processes, usize)) {
        seL4_SetMR (0, i);
    } else {
        seL4_SetMR (0, 0);
    }

    return PAWPAW_EVENT_NEEDS_REPLY;
}