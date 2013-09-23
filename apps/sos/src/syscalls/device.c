#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <mapping.h>

seL4_MessageInfo_t syscall_register_irq (thread_t thread) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);

    /* FIXME: probably want to copy instead, so that we can revoke later if needed */
    seL4_CPtr irq_cap = cspace_irq_control_get_cap(cur_cspace, seL4_CapIRQControl, seL4_GetMR(1));
    if (!irq_cap) {
        seL4_SetMR (0, 0);
        return reply;
    }

    seL4_CPtr their_cap = cspace_copy_cap (thread->croot, cur_cspace, irq_cap, seL4_AllRights);
    seL4_SetMR (0, their_cap);

    return reply;
}

seL4_MessageInfo_t syscall_map_device (thread_t thread) {
    seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, (seL4_Word)map_device_thread ((void*)seL4_GetMR (1), seL4_GetMR (2), thread));
    return reply;
}