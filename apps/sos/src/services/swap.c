#include <stdlib.h>
#include <string.h>

#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <pawpaw.h>

#include <vm/vm.h>
#include <vm/addrspace.h>
#include <services/services.h>

#include "ut_manager/ut.h"

#include <elf/elf.h>
#include "elf.h"

#include "vm/vmem_layout.h"

#include <assert.h>
#include <sys/panic.h>
#include <vfs.h>

#include <cpio/cpio.h>
#include <vm/pagetable.h>
#include <vm/frametable.h>

seL4_CPtr _mmap_ep;
extern seL4_CPtr _badgemap_ep;	/* XXX */
extern seL4_CPtr rootserver_async_cap;

struct mmap_queue_item {
	struct pt_entry *page;
	void (*cb)(struct pawpaw_event *evt);
	struct pawpaw_event *evt;

	struct mmap_queue_item *next;
};

struct mmap_queue_item* mmap_queue = NULL;
struct mmap_queue_item* done_queue = NULL;

extern seL4_CPtr rootserver_syscall_cap;


void mmap_item_dispose (struct mmap_queue_item *q);
void mmap_item_success (struct mmap_queue_item* q);
void mmap_item_fail (struct mmap_queue_item *q);
struct mmap_queue_item*
mmap_queue_new (struct pt_entry* page, void* callback, struct pawpaw_event* evt);

static int
mmap_queue_schedule (int direction, vaddr_t vaddr, struct pt_entry* page, void* callback, struct pawpaw_event* evt);

void mmap_item_dispose (struct mmap_queue_item *q) {
	struct mmap_queue_item* v = mmap_queue;
    struct mmap_queue_item* prev = NULL;
    while (v) {
    	if (v->next == q) {
    		prev = v;
    		break;
    	}

    	v = v->next;
    }

    if (prev) {
    	prev->next = q->next;
    } else {
    	mmap_queue = q->next;
    }

    free (q);
}

struct mmap_queue_item*
mmap_queue_new (struct pt_entry* page, void* callback, struct pawpaw_event* evt) {
	struct mmap_queue_item* q = malloc (sizeof (struct mmap_queue_item));
	if (!q) {
		return NULL;
	}

	q->page = page;
	q->cb = callback;
	q->evt = evt;

	/* add it */
	q->next = mmap_queue;
	mmap_queue = q;

	return q;
}


/* FIXME: should be dictionary */
struct seen_item {
	seL4_CPtr cap;
	seL4_Word id;
	struct seen_item *next;
};

struct seen_item* regd_caps = NULL;

/* FIXME: think about checking the queue before deleting threads, otherwise race */
static int
mmap_queue_schedule (int direction, vaddr_t vaddr, struct pt_entry* page, void* callback, struct pawpaw_event* evt) {
	struct mmap_queue_item* q = mmap_queue_new (page, callback, evt);
	if (!q) {
		return false;
	}

	assert (page);
	assert (page->frame);

	seL4_CPtr cap;
	struct seen_item *item = regd_caps;
	while (item) {
		if (item->cap == page->frame->file) {
			break;
		}

		item = item->next;
	}

	if (!item) {
		seL4_MessageInfo_t reg_msg = seL4_MessageInfo_new (0, 0, 1, 1);

		/* FIXME: infoleak - don't use virtual addresses as ids */
	    seL4_CPtr their_cap = cspace_mint_cap (
	    	cur_cspace, cur_cspace, _mmap_ep, seL4_AllRights, seL4_CapData_Badge_new (page));

	    assert (their_cap);

		seL4_SetMR (0, VFS_REGISTER_CAP);
		seL4_SetCap (0, their_cap);
		printf ("mmap: registering cap\n");
		seL4_Call (page->frame->file, reg_msg);
		seL4_Word id = seL4_GetMR (0);
		printf ("mmap: got new cap 0x%x\n", id);
		assert (id > 0);

		item = malloc (sizeof (struct seen_item));
		assert (item);
		item->cap = page->frame->file;
		item->id = id;

		item->next = regd_caps;
		regd_caps = item;
	}

	if (direction == PAGE_SWAP_IN) {
	    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);
	    /* memmap the file - FIXME: should be async -> TO SWAP THREAD */

	    seL4_Word id = cid_next ();
	    printf ("-> into vaddr 0x%x with pid %d\n", vaddr, ((thread_t)evt->args[1])->pid);
	    /* FIXME: this is dodgy */
	    maps_append (id, ((thread_t)evt->args[1])->pid, vaddr);

	    /* well, better badge.. better call Saul */
	    seL4_CPtr badge_cap = cspace_mint_cap (
	    	cur_cspace, cur_cspace, _badgemap_ep, seL4_AllRights, seL4_CapData_Badge_new (id));
	    assert (badge_cap);

	    msg = seL4_MessageInfo_new (0, 0, 1, 7);
	    seL4_SetCap (0, badge_cap);
	    seL4_SetMR (0, VFS_READ_OFFSET);
	    seL4_SetMR (1, id);
	    seL4_SetMR (2, page->frame->load_length);
	    seL4_SetMR (3, page->frame->file_offset);
	    seL4_SetMR (4, page->frame->load_offset);
	    seL4_SetMR (5, item->id);	/* async ID */
	    seL4_SetMR (6, page);

	    printf ("Calling file %d @ offset 0x%x /w vm offset 0x%x\n", page->frame->file, page->frame->file_offset, page->frame->load_offset);
	    seL4_Send (page->frame->file, msg);

	    /* and we go back to waiting on our EP */
	} else if (direction == PAGE_SWAP_OUT) {
		printf ("%s: swapping out not implemented yet\n");
		return false;
	} else {
		printf ("%s: unknown swap direction 0x%x; ignoring\n", __FUNCTION__, direction);
		return false;
	}

	return true;
}

/* avoids the need for sync primitives */
int
mmap_swap (int direction, vaddr_t vaddr, struct pt_entry* page, void* callback, struct pawpaw_event* evt) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 6);

	seL4_SetMR (0, MMAP_REQUEST);
	seL4_SetMR (1, direction);
	seL4_SetMR (2, vaddr);
	seL4_SetMR (3, page);
	seL4_SetMR (4, callback);
	seL4_SetMR (5, evt);

	/* note: cannot be a Call since svc_mmap might require the root server
	 * to be free to handle some of its own requests, eg sbrk */
	seL4_Send (_mmap_ep, msg);

	return true;
}

/*
 * mmap service 
 */
int mmap_main (void) {
    while (1) {
    	seL4_Word badge = 0;
		seL4_Wait (_mmap_ep, &badge);

		if (badge == 0) {
			/* request from rootsvr, handle it */
			seL4_Word method = seL4_GetMR (0);
			if (method == MMAP_REQUEST) {
				/* i've got a nest of brackets, but no bird... */
				mmap_queue_schedule (
					seL4_GetMR (1), seL4_GetMR (2), seL4_GetMR (3),
					seL4_GetMR (4), seL4_GetMR (5));
			} else if (method == MMAP_RESULT) {
				seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 2);
				if (!done_queue) {
					seL4_SetMR (0, 0);
					seL4_SetMR (1, 0);
				} else {
					seL4_SetMR (0, done_queue->cb);
					seL4_SetMR (1, done_queue->evt);

					struct mmap_queue_item* cur = done_queue;
					done_queue = done_queue->next;
					free (cur);
				}

				seL4_Reply (reply);
			} else {
				panic ("unknown request from rootsvr\n");
			}
		} else {
			/* response from filesystem */
			seL4_Word amount = seL4_GetMR (0);
			seL4_Word evt = seL4_GetMR (1);

			/* find the matching mmap request */
			struct mmap_queue_item* q = mmap_queue;
			while (q) {
				if (q->page == evt) {
					/* FIXME: this is inefficient, should just manipulate pointers */
					struct mmap_queue_item* r = malloc (sizeof (struct mmap_queue_item));
					if (!r) {
						q = NULL;
						break;
					}

					r->page = q->page;
					r->cb = q->cb;
					r->evt = q->evt;
					/* FIXME: need status flag for read >= 0 */

					/* free old */
					mmap_item_dispose (q);

					/* insert r into done */
					r->next = done_queue;
					done_queue = r;

					break;
				}

				q = q->next;
			}

			/* read finished, notify server if we found one */
			if (q == NULL) {
				printf ("mmap: unknown badge 0x%x + ID 0x%x - how did you get this number\n", badge, evt);
			} else {
				seL4_Notify (rootserver_async_cap, MMAP_IRQ);
			}
		}
	}

	return 0;
}