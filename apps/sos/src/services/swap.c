#include <stdlib.h>
#include <string.h>

#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <pawpaw.h>

#include <vm/vm.h>
#include <vm/addrspace.h>

#include "ut_manager/ut.h"

#include <elf/elf.h>
#include "elf.h"

#include "vm/vmem_layout.h"

#include <assert.h>
#include <vfs.h>

#include <cpio/cpio.h>
#include <vm/pagetable.h>
#include <vm/frametable.h>

seL4_CPtr _mmap_ep;
extern seL4_CPtr _badgemap_ep;	/* XXX */

struct mmap_queue_item {
	struct pt_entry *page;
	void (*cb)(struct pawpaw_event *evt);
	struct pawpaw_event *evt;

	struct mmap_queue_item *next;
};

struct mmap_queue_item* mmap_queue = NULL;

extern seL4_CPtr rootserver_syscall_cap;



void mmap_item_dispose (struct mmap_queue_item *q);
void mmap_item_success (struct mmap_queue_item* q);
void mmap_item_fail (struct mmap_queue_item *q);
struct mmap_queue_item*
mmap_queue_new (struct pt_entry* page, void* callback, struct pawpaw_event* evt);

static int
mmap_queue_schedule (int direction, vaddr_t vaddr, struct pt_entry* page, void* callback, struct pawpaw_event* evt);

void mmap_item_success (struct mmap_queue_item* q) {
	q->page->flags &= ~PAGE_SWAPPING;
	q->page->flags |= PAGE_ALLOCATED;

	/* FIXME: do we need this for ALL? */
    seL4_ARM_Page_FlushCaches (q->page->cap);

    /* call their callback */
    /*if (q->cb) {
    	q->cb (q->evt);
    } else {
    	printf ("%s: NULL callback\n", __FUNCTION__);
    }*/

    printf ("disposed\n");
    mmap_item_dispose (q);
}

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

void mmap_item_fail (struct mmap_queue_item *q) {
	seL4_SetMR (0, -1);
	seL4_Call (q->evt->reply_cap, q->evt->reply);
	//syscall_event_dipose (q->evt);
	/* FIXME: call dispose */

	mmap_item_dispose (q);
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

/* FIXME: think about checking the queue before deleting threads, otherwise race */

static int
mmap_queue_schedule (int direction, vaddr_t vaddr, struct pt_entry* page, void* callback, struct pawpaw_event* evt) {
	struct mmap_queue_item* q = mmap_queue_new (page, callback, evt);
	if (!q) {
		return false;
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

	    msg = seL4_MessageInfo_new (0, 0, 1, 5);
	    seL4_SetCap (0, badge_cap);
	    seL4_SetMR (0, VFS_READ_OFFSET);
	    seL4_SetMR (1, id);
	    seL4_SetMR (2, PAGE_SIZE);
	    seL4_SetMR (3, page->frame->offset);
	    seL4_SetMR (4, 1);	/* async */

	    printf ("Calling file %d @ offset 0x%x\n", page->frame->file, page->frame->offset);
	    seL4_Call (page->frame->file, msg);

	    /* and give it our async cap */
	    msg = seL4_MessageInfo_new (0, 0, 1, 0);

	    /* FIXME: try not to create a zillion caps */
	    /* FIXME: infoleak - don't use virtual addresses as ids */
	    seL4_CPtr their_cap = cspace_mint_cap (
	    	cur_cspace, cur_cspace, _mmap_ep, seL4_AllRights, seL4_CapData_Badge_new (page));

	    assert (their_cap);

	    seL4_SetCap (0, their_cap);
	    //seL4_SetMR (0, VFS_READ_OFFSET + 1);	/* XXX define */
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

/* avoid the need for sync primitives */
int
mmap_swap (int direction, vaddr_t vaddr, struct pt_entry* page, void* callback, struct pawpaw_event* evt) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 5);
	seL4_SetMR (0, direction);
	seL4_SetMR (1, vaddr);
	seL4_SetMR (2, page);
	seL4_SetMR (3, callback);
	seL4_SetMR (4, evt);

	printf ("Supposedly sending to mmap service...\n");
	seL4_Send (_mmap_ep, msg);
	//return seL4_GetMR (0);
	return true;
}


/*
 * MMapper - makes disk stuff async
 *
 * on mmap in, sends page pointer, ID 
	- main service should call callback on ID received
 * on mmap out (ie swap), sends ???, ID
 	- main service should call callback on ID received
 */
int mmap_main (void) {
	/*printf ("fs_cpio: starting\n");	
    pawpaw_event_init ();*/

	//pawpaw_event_loop (&handler_table, NULL, service_ep);

    while (1) {
    	seL4_Word badge = 0;
    	printf ("###### mmap: waiting\n");
		seL4_Wait (_mmap_ep, &badge);

		if (badge == 0) {
			/* request from rootsvr, handle it */
			seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 1);

			printf ("mmap: Received request from rootsvr\n");

			/* i've got a nest, but no bird... */
			seL4_SetMR (0, mmap_queue_schedule (
				seL4_GetMR (0), seL4_GetMR (1), seL4_GetMR (2),
				seL4_GetMR (3), seL4_GetMR(4)));

			seL4_Reply (reply);
		} else {
			struct mmap_queue_item* q = mmap_queue;
			while (q) {
				if (q->page == badge) {
					printf ("mmap success\n");
					printf ("received 0x%x bytes\n", seL4_GetMR (0));
					seL4_MessageInfo_t msg = seL4_MessageInfo_new (42, 0, 0, 2);

					seL4_SetMR (0, q->cb);
					seL4_SetMR (1, q->evt);
					mmap_item_success (q);

					printf ("#### SENT TO ROOTSVR\n");
					seL4_Send (rootserver_syscall_cap, msg);

					break;
				}

				q = q->next;
			}

			if (q == NULL) {
				/* read finished, notify server */
				printf ("mmap: unknown badge %d - how did you get this number\n");
			}
		}
	}

	return 0;
}