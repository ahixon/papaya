#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>

extern thread_t current_thread;

int syscall_alloc_cnodes (struct pawpaw_event* evt) {
	evt->reply = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_CPtr root_cptr = 0;

    seL4_SetMR (1, thread_cspace_new_cnodes (current_thread, evt->args[0],
    	&root_cptr));

    seL4_SetMR (0, root_cptr);	/* root_cptr depends on above; don't reorder */

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_create_ep_sync (struct pawpaw_event* evt) {
	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
	seL4_SetMR (0, thread_cspace_new_ep (current_thread));

	return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_create_ep_async (struct pawpaw_event* evt) {
	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
	seL4_SetMR (0, thread_cspace_new_async_ep (current_thread));

	return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_bind_async_tcb (struct pawpaw_event* evt) {
	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

	seL4_CPtr our_cap = cspace_copy_cap (cur_cspace, current_thread->croot,
		evt->args[0], seL4_AllRights);
	
	if (!our_cap) {
		seL4_SetMR (0, 0);
	} else {
		seL4_SetMR (0, seL4_TCB_BindAEP (current_thread->tcb_cap, our_cap));
	}

	return PAWPAW_EVENT_NEEDS_REPLY;
}