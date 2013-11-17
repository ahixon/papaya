#include <sel4/sel4.h>
#include <string.h>
#include <stdlib.h>

#include <pawpaw.h>
#include <syscalls.h>
#include <stdio.h>

/*
 * http://www.youtube.com/watch?v=XhBSgCiaPDQ
 */

extern union header  *_kr_malloc_freep;

struct pawpaw_share* pawpaw_share_new (void) {
	sos_debug_print ("pawpaw: hello from share_new\n", strlen ("pawpaw: hello from share_new\n"));

    /*printf ("header addr = 0x%x\n", (unsigned int)&_kr_malloc_freep);
    printf ("header points to = 0x%x\n", (unsigned int)_kr_malloc_freep);
    printf ("header val = 0x%x\n", *(char*)_kr_malloc_freep);*/

	struct pawpaw_share* share = malloc (sizeof (struct pawpaw_share));
	if (!share) {
		sos_debug_print ("pawpaw: malloc failed\n", strlen ("pawpaw: malloc failed\n"));
		return NULL;
	}

	sos_debug_print ("pawpaw: malloc OK sending msg\n", strlen ("pawpaw: malloc OK sending msg\n"));
	memset (share, 0, sizeof (struct pawpaw_share));

	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, SYSCALL_SHARE_CREATE);
    
    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);

    /* FIXME: strictly speaking, check length as well */
    if (seL4_MessageInfo_get_label (reply) == seL4_NoError && seL4_GetMR (0) != 0) {
    	share->cap = seL4_GetMR (0);
    	share->id = seL4_GetMR (1);
    	share->buf = (void*)seL4_GetMR (2);
    	share->loaded = true;
    	share->sent = false;

    	/* FIXME: lookup seL4_GetMR (3) and mark as unloaded */
    	return share;
    } else {
    	free (share);
    	return NULL;
    }
}

struct pawpaw_share* pawpaw_share_mount (seL4_CPtr cap) {
	struct pawpaw_share* share = malloc (sizeof (struct pawpaw_share));
	if (!share) {
		return NULL;
	}

	memset (share, 0, sizeof (struct pawpaw_share));

	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_SHARE_MOUNT);
    seL4_SetMR (1, cap);
    
    seL4_MessageInfo_t reply = seL4_Call (PAPAYA_SYSCALL_SLOT, msg);

    if (seL4_MessageInfo_get_label (reply) == seL4_NoError && seL4_MessageInfo_get_length (reply) == 3) {
    	share->cap = cap;
    	share->id = seL4_GetMR (0);
    	share->buf = (void*)seL4_GetMR (1);
    	share->loaded = true;
    	share->sent = false;

    	/* FIXME: lookup seL4_GetMR (2) and mark as unloaded */
    	return share;
    } else {
    	free (share);
    	return NULL;
    }
}

int pawpaw_share_unmount (struct pawpaw_share* share) {
	if (!share || !share->cap) {
		return false;
	}

	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, SYSCALL_SHARE_UNMOUNT);
    seL4_SetMR (1, share->cap);
    seL4_SetMR (2, (seL4_Word)share->buf);
    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);

    if (seL4_GetMR (0)) {
    	/* root server deletes, so mark as free locally */
    	pawpaw_cspace_free_slot (share->cap);

		free (share);
		return true;
	} else {
		return false;
	}
}

/*
 * attaches the share cap to the current seL4 message.
 * returns the ID of the next index to add caps to in the message.
 */
int pawpaw_share_attach (struct pawpaw_share* share) {
	if (!share->sent) {
		seL4_SetCap (0, share->cap);
		share->sent = true;
		return 1;
	}

	return 0;
}

/* FIXME: binary tree or heap would be better - IDK THIS SHOULD JUST BE A HASHMAP */
struct share_info {
	struct pawpaw_share *share;
	struct share_info* next;
};

struct share_info* share_head;

struct pawpaw_share* pawpaw_share_get (seL4_Word id) {
	struct share_info* share = share_head;
	while (share) {
		if (share->share->id == id) {
			return share->share;
		}

		share = share->next;
	}

	return NULL;
}

void pawpaw_share_set (struct pawpaw_share* share) {
	struct share_info* si = malloc (sizeof (struct share_info));
	if (!si) {
		return;
	}

	si->share = share;
	si->next = share_head;
	share_head = si;
}

void pawpaw_share_unset (struct pawpaw_share* share) {
	struct share_info* prev = NULL;
	struct share_info* container = NULL;
	struct share_info* s = share_head;

	while (s) {
		if (s->share == share) {
			container = s;
			break;
		} else if (s->next && s->next->share == share) {
			prev = s;
		}

		s = s->next;
	}

	if (prev) {
		prev->next = container->next;
	} else {
		share_head = container->next;
	}
}