#include <pawpaw.h>
#include <sel4/sel4.h>
#include <thread.h>
#include <stdio.h>
#include <copyinout.h>

#define THREAD_PATH_SIZE_MAX	512

extern thread_t current_thread;
extern seL4_CPtr rootserver_syscall_cap;

int syscall_thread_suicide (struct pawpaw_event* evt) {
	printf ("\nthread %s asked to terminate\n", current_thread->name);
	thread_destroy (current_thread);

    return PAWPAW_EVENT_HANDLED;
}

int syscall_thread_create (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    char thread_path[THREAD_PATH_SIZE_MAX];
    if (!copyin (current_thread, evt->args[0], evt->args[1], thread_path, THREAD_PATH_SIZE_MAX)) {
        /* invalid request, ignore */
        return PAWPAW_EVENT_UNHANDLED;
    }

    printf ("\nthread %s asked to create thread with path '%s'\n", current_thread->name, thread_path);
    thread_t child = thread_create_from_fs (thread_path, rootserver_syscall_cap);
    if (!child) {
    	seL4_SetMR (0, child->pid);
    } else {
    	seL4_SetMR (0, -1);
    }

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_thread_destroy (struct pawpaw_event* evt) {
	thread_t target = thread_lookup (evt->args[0]);

	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

	if (target) {
		printf ("\nthread %s asked to terminate PID %d (%s)\n",
			current_thread->name, target->pid, target->name);

		thread_destroy (target);
		seL4_SetMR (0, 0);
	} else {
		printf ("\nthread %s asked to kill PID %d but no such thread\n",
			current_thread->name, evt->args[0]);
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
    return PAWPAW_EVENT_UNHANDLED;
}