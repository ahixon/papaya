#include <sel4/sel4.h>
#include <stdlib.h>

#include <pawpaw.h>

#define DEFAULT_SLOT_REQUEST_SIZE		(32)

struct CNodeInfo {
	seL4_CPtr ptr;
	struct CNodeInfo *next;
};

/* Linked list pool of unallocated cspaces */
struct CNodeInfo* unallocated;

seL4_CPtr pawpaw_request_cspace_slots (int num) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_ALLOC_CNODES);
    seL4_SetMR (1, num);

    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);

    if (seL4_MessageInfo_get_label (reply) == seL4_NoError) {
    	return seL4_GetMR (0);
    } else {
    	return 0;
    }
}

/* returns a CNode back to the root server to put back in our CSpace */
static void unallocated_unalloc () {
	/* TODO: implement this function! */
	//assert (!"IMPLEMENT ME");
}

static seL4_CPtr unallocated_pop (void) {
	seL4_CPtr ret = 0;

	if (unallocated) {
		ret = unallocated->ptr;
		struct CNodeInfo* next = unallocated->next;

		free (unallocated);
		unallocated = next;
	}

	return ret;
}

static int unallocated_push (seL4_CPtr cn) {
	struct CNodeInfo *info = malloc (sizeof (struct CNodeInfo));
	if (!info) {
		return false;
	}

	info->ptr = cn;

	if (unallocated) {
		info->next = unallocated;
	} else {
		info->next = NULL;
	}

	unallocated = info;
	return true;
}

static int fill_unallocated_pool (int remaining) {
	int allocated = 0;
	int i;

	while (remaining > 0) {
		seL4_CPtr slot_root = pawpaw_request_cspace_slots (remaining);
		if (!slot_root) {
			return 0;
		}

		seL4_Word block_allocated = seL4_GetMR (1);
		if (block_allocated == 0) {
			/* if root server gives us no more, stop trying */
			break;
		}

		allocated += block_allocated;
		remaining -= block_allocated;

		for (i = 0; i < block_allocated; i++) {
			if (!unallocated_push (slot_root + i)) {
				/* failed to push, so to root server to unalloc */
				unallocated_unalloc (slot_root + i);
			}
		}
	}

	return allocated > 0;
}

seL4_CPtr pawpaw_cspace_alloc_slot (void) {
	/* out of free cnodes, try to get some more */
	if (!unallocated) {
		if (!fill_unallocated_pool (DEFAULT_SLOT_REQUEST_SIZE)) {
			/* root server refuses to give us *any* more slots */
			return 0;
		}
	}

	return unallocated_pop ();
}

void pawpaw_cspace_free_slot (seL4_CPtr cn) {
	/* just add it right back into the pool */
	unallocated_push (cn);
}