#include <pawpaw.h>
#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>

#include <vm/vm.h>

extern thread_t current_thread;

int syscall_sbrk (struct pawpaw_event* evt) {
	vaddr_t new_addr = as_resize_heap (current_thread->as, evt->args[0]);

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    printf ("####### SBRK RETURNING 0x%x\n", new_addr);
    seL4_SetMR (0, new_addr);

    return PAWPAW_EVENT_NEEDS_REPLY;
}