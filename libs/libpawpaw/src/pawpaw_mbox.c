#include <sel4/sel4.h>
#include <string.h>
#include <stdlib.h>

#include <pawpaw.h>
#include <syscalls.h>

/*
 * http://www.youtube.com/watch?v=XhBSgCiaPDQ
 */

struct sbuf_slot {
	unsigned int idx;

	struct sbuf_slot* next;
};

struct sbuf {
	seL4_Word id;
	seL4_CPtr cap;
	void* slots;
	unsigned int size;

	struct sbuf_slot* pinned_slots;
	unsigned int last_slot;
	short used;
};

sbuf_t pawpaw_sbuf_create (unsigned int size) {
	struct sbuf* sb = malloc (sizeof (struct sbuf));

	if (!sb) {
		return NULL;
	}

	memset (sb, 0, sizeof (struct sbuf));

	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_SHAREDBUF_CREATE);
    seL4_SetMR (1, size);
    
    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);

    /* FIXME: strictly speaking, check length as well */
    if (seL4_MessageInfo_get_label (reply) == seL4_NoError && seL4_GetMR (0) != 0) {
    	sb->cap = seL4_GetMR (0);
    	sb->slots = (void*)seL4_GetMR (1);
    	sb->size = seL4_GetMR (2);	/* since we might not always get the size we requested */
    	sb->id = seL4_GetMR (3);

    	sb->pinned_slots = NULL;
    	sb->last_slot = 0;
    	sb->used = false;

    	if (!pawpaw_sbuf_install (sb)) {
    		free (sb);
    		/* FIXME: revoke sb */
    		return NULL;
    	}

    	return sb;
    } else {
    	free (sb);
    	return NULL;
    }
}

sbuf_t pawpaw_sbuf_mount (seL4_CPtr cap) {
	if (cap == 0) {
		return NULL;
	}

	struct sbuf* sb = malloc (sizeof (struct sbuf));

	if (!sb) {
		return NULL;
	}

	memset (sb, 0, sizeof (struct sbuf));

	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_SHAREDBUF_MOUNT);
    seL4_SetMR (1, cap);
    
    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    /* FIXME: strictly speaking, check length as well */
    if (seL4_MessageInfo_get_label (reply) == seL4_NoError && seL4_GetMR (0) != 0) {
    	sb->cap = cap;
    	sb->slots = (void*)seL4_GetMR (0);
    	sb->size = seL4_GetMR (1);
    	sb->id = seL4_GetMR (2);

    	sb->pinned_slots = NULL;
    	sb->last_slot = 0;
    	sb->used = false;

    	if (!pawpaw_sbuf_install (sb)) {
    		free (sb);
    		/* FIXME: revoke sb */
    		return NULL;
    	}

    	return sb;
    } else {
    	free (sb);
    	return NULL;
    }

}

inline void* pawpaw_sbuf_slot_get (sbuf_t sb, unsigned int idx) {
	if (idx >= sb->size) {
		return NULL;
	}

	sb->last_slot = idx;

	return (void*)(sb->slots + (idx * PAPAYA_BEAN_SIZE));
}

inline seL4_Word pawpaw_sbuf_get_id (sbuf_t sb) {
	return sb->id;
}

int pawpaw_sbuf_slot_next (sbuf_t sb) {
	unsigned int next = sb->last_slot + 1;

	/* wrap around */
	if (next >= sb->size) {
		next = 0;
	}

#if 0
	unsigned int initial_next = next;
	short ok = true;
	struct sbuf_slot* pin = sb->pinned_slots;

	/* check to see if we picked a pinned page */
	while (pin) {
		if (next == pin->idx) {
			next++;

			/* check if we ended up back at the start */
			if (next == initial_next) {
				ok = false;
				break;
			}

			/* start the pin check again with the new ID */
			pin = sb->pinned_slots;
		}

		/* this pin was OK, check the next one */
		pin = pin->next;
	}

	if (ok) {
		return next;
	} else {
		/* FIXME: just make all the damn IDs signed instead of unsigned */
		return -1;
	}
#endif

	sb->last_slot = next;

	return next;	/* KISS */
}

seL4_CPtr pawpaw_sbuf_get_cap (sbuf_t sb) {
	return sb->cap;
}

struct sbuf_info {
	//seL4_Word id;
	sbuf_t buffer;

	struct sbuf_info* next;
};

struct sbuf_info* buffers;

int pawpaw_sbuf_install (sbuf_t sb) {
	struct sbuf_info* sbi = malloc (sizeof (struct sbuf_info));
	if (!sbi) {
		return false;
	}

	//sbi->id = idx;
	sbi->buffer = sb;

	/* ahhhh, push it, push it REAAAAL GOOD */
	if (buffers) {
		sbi->next = buffers;
	} else {
		sbi->next = NULL;
	}

	buffers = sbi;
	return true;
}

/* FIXME: again, should be hash table or binary tree, not LL - O(n) sucks */
sbuf_t pawpaw_sbuf_fetch (seL4_Word idx) {
	struct sbuf_info* sbi = buffers;
	while (sbi) {
		if (sbi->buffer->id == idx) {
			return sbi->buffer;
		}

		sbi = sbi->next;
	}

	return NULL;
}

#if 0

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

seL4_CPtr shared_cap = 0;

void* pawpaw_map_in_shared (seL4_CPtr cap) {
	/* currently mapped still */
	if (shared_cap) {
		return NULL;
	}

	if (!seL4_ARM_Page_Map (cap,
		PAPAYA_PAGEDIR_SLOT, PAPAYA_SHARED_PAGE_IN_VADDR,
		seL4_AllRights, seL4_ARM_Default_VMAttributes)) {
		return NULL;
	}

	return (void*)PAPAYA_SHARED_PAGE_IN_VADDR;
}

int pawpaw_map_out_shared (void) {
	if (!seL4_ARM_Page_Unmap (shared_cap)) {
		return false;
	}

	pawpaw_cspace_free_slot (shared_cap);
	return true;
}

#endif