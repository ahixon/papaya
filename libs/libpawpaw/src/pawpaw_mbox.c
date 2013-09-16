#include <sel4/sel4.h>
#include <string.h>
#include <stdlib.h>

#include <pawpaw.h>

/*
 * http://www.youtube.com/watch?v=XhBSgCiaPDQ
 * Can you do the can-can?
 */

/* negotiates to create a pawpaw can - NOTE THIS MAY NEVER RETURN if the other party refuses to talk back
 * "other" is the CPtr to the cap that has an open EP - you get this using find_service */
struct pawpaw_can* pawpaw_can_negotiate (seL4_CPtr other, unsigned int max_beans) {
	struct pawpaw_can* can = malloc (sizeof (struct pawpaw_can));

	if (!can) {
		return NULL;
	}

	memset (can, 0, sizeof (struct pawpaw_can));
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 4);
    seL4_SetMR (0, SYSCALL_CAN_NEGOTIATE);
    seL4_SetMR (1, other);
    seL4_SetMR (2, max_beans);
    seL4_SetMR (3, (seL4_Word)can);

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    if (seL4_MessageInfo_get_label (reply) == seL4_NoError && can->count > 0) {
    	return can;
    } else {
    	free (can);
    	return NULL;
    }
}

/* FIXME: in the future, block-ify this - SINCE LINEAR SEARCH SUCKS!! */
struct can_info {
	struct pawpaw_can* can;
	seL4_Word id;

	struct can_info* next;
};

struct can_info* cans;

/* FIXME: what if can negotiation fails? need to free this somehow */
struct pawpaw_can* pawpaw_can_allocate (seL4_Word id) {
	struct pawpaw_can* can = malloc (sizeof (struct pawpaw_can));
	if (!can) {
		return NULL;
	}

    memset (can, 0, sizeof (struct pawpaw_can));


    return pawpaw_can_set (id, can);
}

struct pawpaw_can* pawpaw_can_set (seL4_Word id, struct pawpaw_can* can) {
	struct can_info* info = malloc (sizeof (struct can_info));
    if (!info) {
    	return NULL;
    }

    info->id = id;
    info->can = can;
    info->next = NULL;

    /* add to head of can stack */
    if (cans) {
    	cans->next = info;
    }

	cans = info;

	return can;
}

struct pawpaw_can* pawpaw_can_fetch (seL4_Word id) {
	struct can_info* info = cans;
	while (info) {
		if (info->id == id) {
			return info->can;
		}

		info = info->next;
	}

	return NULL;
}

inline void* pawpaw_bean_get (struct pawpaw_can* can, unsigned int bean_idx) {
	if (bean_idx >= can->count) {
		return NULL;
	}

	return (void*)(can->start + (bean_idx * PAPAYA_BEAN_SIZE));
}