#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <mapping.h>

extern thread_t current_thread;

int syscall_register_irq (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

    /* FIXME: probably want to copy instead, so that we can revoke later if needed */
    seL4_CPtr irq_cap = cspace_irq_control_get_cap (cur_cspace, seL4_CapIRQControl, evt->args[0]);
    if (!irq_cap) {
        seL4_SetMR (0, 0);
    } else {
        seL4_CPtr their_cap = cspace_copy_cap (current_thread->croot, cur_cspace, irq_cap, seL4_AllRights);
        seL4_SetMR (0, their_cap);
    }

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_map_device (struct pawpaw_event* evt) {
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, (seL4_Word)map_device_thread ((void*)evt->args[0], evt->args[1], current_thread));

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int syscall_alloc_dma (struct pawpaw_event* evt) {
    /* FIXME: implement me! */
    return PAWPAW_EVENT_UNHANDLED;
}