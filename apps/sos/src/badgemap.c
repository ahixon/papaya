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

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>
#include <assert.h>

#define DEFAULT_PRIORITY		(0)

/* FIXME: terrible name */
struct map {
	seL4_Word idx;
	pid_t pid;
	vaddr_t start;

	struct map* next;
};

static struct map* maps_head = NULL;
//static struct map* maps_tail = NULL;

short badgemap_found = false;

volatile seL4_CPtr _badgemap_ep = 0;

/*
 * internal info service
 */
int mapper_main (void) {
	printf ("Hello from badgemap!\n");
	
	while (1) {
		seL4_Word badge;
		seL4_Wait (_badgemap_ep, &badge);
		badgemap_found = false;

		/* FIXME: yuck linear search through list */
		struct map* m = maps_head;
		while (m) {
			if (m->idx == badge) {
				seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 3);

				seL4_SetMR (0, m->pid);
				seL4_SetMR (1, m->start);
				seL4_SetMR (2, badge);

				badgemap_found = true;
				seL4_Reply (msg);
				break;
			}

			m = m->next;
		}

		if (!m) {
			printf ("badgemap: found no map for share idx 0x%x\n", badge);
		}
	}
}

/* FIXME: hashmap would be MUCH better! */
void maps_append (seL4_Word idx, pid_t pid, vaddr_t start) {
	//printf ("!!!!!!! %s: adding to badgemap !!!!!!!!! WE ARE LEAKING JIM\n", __FUNCTION__);
	/* FIXME: needs a free! */
	struct map *mm = malloc (sizeof (struct map));
	mm->idx = idx;
	mm->pid = pid;
	mm->start = start;
	mm->next = NULL;

	//printf ("badgemap: inserting with share idx 0x%x\n", idx);

	/*if (maps_tail) {
		maps_tail->next = mm;
	}

	if (!maps_head) {
		maps_head = mm;
	}

	maps_tail = mm;*/

	/* push onto stack - better performance since most of the time we just
	 * map straight away */
	mm->next = maps_head;
	maps_head = mm;
}