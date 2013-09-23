#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>

seL4_MessageInfo_t syscall_alloc_cnodes (thread_t thread) {
	seL4_MessageInfo_t reply = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_CPtr root_cptr;

    seL4_SetMR (1, thread_cspace_new_cnodes (thread, seL4_GetMR (1), &root_cptr));
    seL4_SetMR (0, root_cptr);

    return reply;
}

seL4_MessageInfo_t syscall_create_ep_sync (thread_t thread) {
	seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);
	seL4_SetMR (0, thread_cspace_new_ep (thread));

	return reply;
}

seL4_MessageInfo_t syscall_create_ep_async (thread_t thread) {
	seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);
	seL4_SetMR (0, thread_cspace_new_async_ep (thread));

	return reply;
}

seL4_MessageInfo_t syscall_bind_async_tcb (thread_t thread) {
	seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);

	seL4_CPtr our_cap = cspace_copy_cap (cur_cspace, thread->croot, seL4_GetMR (1), seL4_AllRights);
	if (!our_cap) {
		seL4_SetMR (0, 0);
		return reply;
	}

    seL4_SetMR (0, seL4_TCB_BindAEP (thread->tcb_cap, our_cap));
	return reply;
}