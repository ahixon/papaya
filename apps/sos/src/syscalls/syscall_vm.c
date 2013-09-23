#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>

#include <vm/vm.h>

seL4_MessageInfo_t syscall_sbrk (thread_t thread) {
	vaddr_t new_addr = as_resize_heap (thread->as, seL4_GetMR (1));

    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, new_addr);

    return reply;
}