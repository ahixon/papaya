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
	//struct pt_entry *page;
	struct frameinfo *frame;
	void (*cb)(struct pawpaw_event *evt, struct frameinfo* frame);
	struct pawpaw_event *evt;

	struct mmap_queue_item *next;
};

struct mmap_queue_item* mmap_queue = NULL;
struct mmap_queue_item* done_queue = NULL;

extern seL4_CPtr rootserver_syscall_cap;

int last_page_id = 0;

void mmap_item_dispose (struct mmap_queue_item *q);
void mmap_item_success (struct mmap_queue_item* q);
void mmap_item_fail (struct mmap_queue_item *q);
struct mmap_queue_item*
mmap_queue_new (struct frameinfo* frame, void* callback, struct pawpaw_event* evt);

static int
mmap_queue_schedule (int direction, vaddr_t vaddr, struct frameinfo* frame, void* callback, struct pawpaw_event* evt);

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
mmap_queue_new (struct frameinfo *frame, void* callback, struct pawpaw_event* evt) {
	struct mmap_queue_item* q = malloc (sizeof (struct mmap_queue_item));
	if (!q) {
		return NULL;
	}

	q->frame = frame;
	q->cb = callback;
	q->evt = evt;

	/* add it */
	q->next = mmap_queue;
	mmap_queue = q;

	return q;
}

struct mmap_queue_item*
mmap_move_done (struct mmap_queue_item *q) {
	/* FIXME: this is inefficient, should just manipulate pointers */
	struct mmap_queue_item* r = malloc (sizeof (struct mmap_queue_item));
	if (!r) {
		return NULL;
	}

	//r->page = q->page;
	r->frame = q->frame;
	r->cb = q->cb;
	r->evt = q->evt;
	/* FIXME: need status flag for read >= 0 */

	/* free old */
	mmap_item_dispose (q);

	/* insert r into done */
	r->next = done_queue;
	done_queue = r;

	return r;
}


/* FIXME: should be dictionary */
struct seen_item {
	seL4_CPtr cap;
	seL4_Word id;
	struct seen_item *next;
};

struct seen_item* regd_caps = NULL;

extern seL4_CPtr swap_cap;
seL4_Word swap_id = 0;

/* FIXME: think about checking the queue before deleting threads, otherwise race
	Following things need to happen:
		- have refcount on thread for "outstanding swap requests"
		- only delete the thread once this reaches 0
			- thread deletion thus needs to be async /w wait queue
			- same goes with thread creation i guess (different issue tho)
 */
static int
mmap_queue_schedule (int direction, vaddr_t vaddr, struct frameinfo *frame, void* callback, struct pawpaw_event* evt) {
	struct mmap_queue_item* q = mmap_queue_new (frame, callback, evt);
	if (!q) {
		return false;
	}

	printf ("** registered with frame %p\n", q->frame);
	assert (frame);

	if (direction == PAGE_SWAP_IN) {
		assert (frame->file);
		assert (frame->file->file);

		/* FIXME: seriously this needs work */
		struct seen_item *item = regd_caps;
		while (item) {
			if (item->cap == frame->file->file) {
			   break;
			}

		   item = item->next;
		}

		if (!item) {
			seL4_MessageInfo_t reg_msg = seL4_MessageInfo_new (0, 0, 1, 1);

			/* FIXME: possible infoleak on using frame kernel vaddr?
			 * DON'T EVEN NEED JUST MINT WITH BADGE > 0 i think */
			seL4_CPtr their_cap = cspace_mint_cap (
				cur_cspace, cur_cspace, _mmap_ep, seL4_AllRights, seL4_CapData_Badge_new ((seL4_Word)frame));

			assert (their_cap);

			seL4_SetMR (0, VFS_REGISTER_CAP);
			seL4_SetCap (0, their_cap);
			printf ("mmap: registering cap on %d\n", frame->file->file);
			seL4_Call (frame->file->file, reg_msg);
			seL4_Word id = seL4_GetMR (0);
			printf ("mmap: got new cap 0x%x\n", id);
			assert (id > 0);

			item = malloc (sizeof (struct seen_item));
			assert (item);
			item->cap = frame->file->file;
			item->id = id;

			item->next = regd_caps;
			regd_caps = item;

			//printf ("mmap: got new cap 0x%x\n", id);
			//swap_id = id;
			//assert (swap_id > 0);
		}

		seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);

		seL4_Word id = cid_next ();
		//printf ("-> into vaddr 0x%x with pid %d\n", vaddr, ((thread_t)evt->args[1])->pid);
		/* FIXME: this is dodgy */
		maps_append (id, ((thread_t)evt->args[1])->pid, vaddr);

		/* well, better badge.. better call Saul */
		seL4_CPtr badge_cap = cspace_mint_cap (
			cur_cspace, cur_cspace, _badgemap_ep, seL4_AllRights, seL4_CapData_Badge_new (id));
		assert (badge_cap);

		assert (frame->file);

		msg = seL4_MessageInfo_new (0, 0, 1, 7);
		seL4_SetCap (0, badge_cap);
		seL4_SetMR (0, VFS_READ_OFFSET);
		seL4_SetMR (1, id);
		seL4_SetMR (2, frame->file->load_length);
		seL4_SetMR (3, frame->file->file_offset);
		seL4_SetMR (4, frame->file->load_offset);
		seL4_SetMR (5, item->id);	/* async ID */
		seL4_SetMR (6, (seL4_Word)frame);

		printf ("Calling file %d @ offset 0x%x /w vm offset 0x%x\n", frame->file->file, frame->file->file_offset, frame->file->load_offset);
		seL4_Send (frame->file->file, msg);

		/* and we go back to waiting on our EP */
	} else if (direction == PAGE_SWAP_OUT) {
		#if 0
		if (!swap_id) {
			seL4_MessageInfo_t reg_msg = seL4_MessageInfo_new (0, 0, 1, 1);

			/* FIXME: possible infoleak on using frame kernel vaddr?
			 * DON'T EVEN NEED JUST MINT WITH BADGE > 0 i think */
			seL4_CPtr their_cap = cspace_mint_cap (
				cur_cspace, cur_cspace, _mmap_ep, seL4_AllRights, seL4_CapData_Badge_new (frame));

			assert (their_cap);

			seL4_SetMR (0, VFS_REGISTER_CAP);
			seL4_SetCap (0, their_cap);
			seL4_Call (swap_cap, reg_msg);
			swap_id = seL4_GetMR (0);
			printf ("mmap: got new cap 0x%x\n", swap_id);
		}
		#endif

		seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 4);

		/* FIXME: need to clean this up after swap OK */
		seL4_Word id = cid_next ();
		maps_append (id, 0, vaddr);

		/* well, better badge.. better call Saul */
		seL4_CPtr badge_cap = cspace_mint_cap (
			cur_cspace, cur_cspace, _badgemap_ep, seL4_AllRights, seL4_CapData_Badge_new (id));
		assert (badge_cap);

		//seL4_Word page_id = frame->paddr;
		seL4_Word page_id = last_page_id * PAGE_SIZE;
		last_page_id++;

		seL4_Word wrote = 0;
		while (wrote < PAGE_SIZE) {
			msg = seL4_MessageInfo_new (0, 0, 1, 7);
			seL4_SetCap (0, badge_cap);
			seL4_SetMR (0, VFS_WRITE_OFFSET);
			seL4_SetMR (1, id);
			seL4_SetMR (2, PAGE_SIZE - wrote);	/* always write the whole page out */
			seL4_SetMR (3, page_id + wrote);	/* file offset */
			seL4_SetMR (4, wrote);		/* load into start of share */
			//seL4_SetMR (5, swap_id);	/* async ID - NOT USED */
			seL4_SetMR (6, (seL4_Word)frame);

			/* and write it out */
			printf ("** calling FS\n");
			seL4_Call (swap_cap, msg);
			seL4_Word wrote_this_call = seL4_GetMR (0);
			assert (wrote_this_call >= 0);
			wrote += wrote_this_call;

			printf ("wrote = 0x%x (total so far = 0x%x)\n", wrote_this_call, wrote);

			for (int i = 0; i < 100; i++) {
				seL4_Yield();
			}
		}

		cspace_delete_cap (cur_cspace, badge_cap);

		/* memory map it */
		frame->file = frame_create_mmap (swap_cap, 0, page_id, PAGE_SIZE);
		assert (frame->file);

		printf ("moving to done queue...\n");
		mmap_move_done (q);
		printf ("done (heh)\n");
		return true;


		//page_dump (frame->page, 0);

		/* and we go back to waiting on our EP */
	} else {
		printf ("%s: unknown swap direction 0x%x; ignoring\n", __FUNCTION__, direction);
	}

	return false;
}

/* avoids the need for sync primitives */
int
mmap_swap (int direction, vaddr_t vaddr, struct frameinfo* frame, void* callback, struct pawpaw_event* evt) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 6);

	seL4_SetMR (0, MMAP_REQUEST);
	seL4_SetMR (1, direction);
	seL4_SetMR (2, vaddr);
	seL4_SetMR (3, (seL4_Word)frame);
	seL4_SetMR (4, (seL4_Word)callback);
	seL4_SetMR (5, (seL4_Word)evt);

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

		int do_reply = false;

		if (badge == 0) {
			/* request from rootsvr, handle it */
			seL4_Word method = seL4_GetMR (0);
			if (method == MMAP_REQUEST) {
				/* i've got a nest of brackets, but no bird... */
				do_reply = mmap_queue_schedule (
					seL4_GetMR (1), seL4_GetMR (2), (struct frameinfo*)seL4_GetMR (3),
					(void*)seL4_GetMR (4), (struct pawpaw_event*)seL4_GetMR (5));

				if (do_reply) {
					seL4_Notify (rootserver_async_cap, MMAP_IRQ);
				}
			} else if (method == MMAP_RESULT) {
				seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 3);
				if (!done_queue) {
					seL4_SetMR (0, 0);
					seL4_SetMR (1, 0);
					seL4_SetMR (2, 0);
				} else {
					seL4_SetMR (0, (seL4_Word)done_queue->cb);
					seL4_SetMR (1, (seL4_Word)done_queue->evt);
					seL4_SetMR (2, (seL4_Word)done_queue->frame);

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
			//seL4_Word amount = seL4_GetMR (0);
			/* FIXME: ensure amount == PAGE_SIZE or needed amount */

			struct frameinfo* evt_id = (struct frameinfo*)seL4_GetMR (1);

			/* find the matching mmap request */
			struct mmap_queue_item* q = mmap_queue;
			while (q) {
				if (q->frame == evt_id) {
					q = mmap_move_done (q);
					break;
				}

				q = q->next;
			}

			/* read finished, notify server if we found one */
			if (q == NULL) {
				printf ("mmap: unknown badge 0x%x + ID %p - how did you get this number\n", badge, evt_id);
			} else {
				seL4_Notify (rootserver_async_cap, MMAP_IRQ);
			}
		}
	}

	return 0;
}