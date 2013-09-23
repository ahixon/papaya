#include <sel4/sel4.h>
#include <string.h>

#include <pawpaw.h>
#include <syscalls.h>

seL4_CPtr pawpaw_register_irq (int irq_num) {
	/* setup a place to receive our IRQHandler cap */
	/*seL4_CPtr cap = pawpaw_cspace_alloc_slot ();  
	if (!cap) {
		return 0;
	}*/

	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    //seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, cap, PAPAYA_CSPACE_DEPTH);

    seL4_SetMR (0, SYSCALL_REGISTER_IRQ);
    seL4_SetMR (1, irq_num);

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);

    /* make sure we got our cap back */
    /*if (seL4_MessageInfo_get_label (reply) == seL4_NoError && 
    	seL4_MessageInfo_get_extraCaps (reply) == 1) {
    	return cap;
    } else {
    	return 0;
    }*/

    if (seL4_MessageInfo_get_label (reply) == seL4_NoError) {
    	return seL4_GetMR (0);
    } else {
    	return 0;
    }
}

seL4_CPtr pawpaw_service_lookup (char* name) {
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 3);

    seL4_SetMR(0, SYSCALL_FIND_SERVICE);
    seL4_SetMR(1, (seL4_Word)name);
    seL4_SetMR(2, strlen (name));

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    if (seL4_MessageInfo_get_label (reply) == seL4_NoError) {
    	return seL4_GetMR(0);
    } else {
        return 0;
    }
}

seL4_CPtr pawpaw_save_reply (void) {
	int err;
	seL4_CPtr cap = pawpaw_cspace_alloc_slot ();
	if (!cap) {
		return 0;
	}

	err = seL4_CNode_SaveCaller (PAPAYA_ROOT_CNODE_SLOT, cap, PAPAYA_CSPACE_DEPTH);
	if (err) {
		/* FIXME: unallocate slot? */
		return 0;
	} else {
		return cap;
	}
}

void* pawpaw_map_device (unsigned int base, unsigned int size) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, SYSCALL_MAP_DEVICE);
    seL4_SetMR (1, base);
    seL4_SetMR (2, size);

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    if (seL4_MessageInfo_get_label (reply) == seL4_NoError) {
    	return (void*)seL4_GetMR (0);
    } else {
    	return NULL;
    }
}

seL4_CPtr pawpaw_create_ep_async (void) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, SYSCALL_CREATE_EP_ASYNC);

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    if (seL4_MessageInfo_get_label (reply) == seL4_NoError) {
    	return seL4_GetMR (0);
    } else {
    	return 0;
    }
}

seL4_CPtr pawpaw_create_ep (void) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, SYSCALL_CREATE_EP_SYNC);

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    if (seL4_MessageInfo_get_label (reply) == seL4_NoError) {
    	return seL4_GetMR (0);
    } else {
    	return 0;
    }
}

int pawpaw_bind_async_to_thread (seL4_CPtr async_ep) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_BIND_AEP_TCB);
    seL4_SetMR (1, async_ep);

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return (seL4_MessageInfo_get_label (reply) == seL4_NoError && seL4_GetMR (0) == 0);
}

int pawpaw_register_service (seL4_CPtr ep) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_REGISTER_SERVICE);
    seL4_SetMR (1, ep);

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return (seL4_MessageInfo_get_label (reply) == seL4_NoError && seL4_GetMR (0) == 0);
}

void pawpaw_suicide (void) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, SYSCALL_SUICIDE);

    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
}