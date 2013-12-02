#include <sel4/sel4.h>
#include <vm/vm.h>
#include <vm/addrspace.h>
#include <vm/vmem_layout.h>
#include <thread.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <assert.h>
#include "mapping.h"

// from elf.c
#define MIN(a,b) (((a)<(b))?(a):(b))

#define PAGESIZE              (1 << (seL4_PageBits))
#define PAGEMASK              ((PAGESIZE) - 1)

/* TODO: unify copyin and copyout, since they share a lot of similar code */

char* copyout (thread_t thread, vaddr_t ubuf, unsigned int usize, char* kbuf,
	unsigned int ksize) {

	assert (kbuf);
	assert (ksize > 0);
	assert (usize > 0);		/* TODO: copyin_str might pass in 0 when impl */

	if (ksize < usize) {
		return NULL;
	}

	/* obvious null */
	if (ubuf == 0) {
		return NULL;
	}

	int err;

	void* kvpage = (void*)PROCESS_SCRATCH;
	unsigned int done = 0;

	seL4_CPtr last_cap = 0;
	seL4_CPtr last_mapped_cap = 0;

	while (done < usize && done < ksize) {
		seL4_CPtr frame_cap = as_get_page_cap (thread->as, ubuf + done);
		if (!frame_cap) {
			/* no page mapped, ABORT - and free buf? */
			return NULL;
		}

		/* only map in page if it's a new one */
		if (last_cap != frame_cap) {
			if (last_cap) {
				/* unmap old one first */
				err = seL4_ARM_Page_Unmap (last_mapped_cap);
				if (err) {
					return NULL;
				}

				cspace_delete_cap (cur_cspace, last_mapped_cap);
			}

			seL4_CPtr copyinout_cap = cspace_copy_cap (cur_cspace, cur_cspace,
				frame_cap, seL4_AllRights);
			if (!copyinout_cap) {
				return NULL;
			}

			last_mapped_cap = copyinout_cap;

			err = map_page (copyinout_cap, seL4_CapInitThreadPD,
				(seL4_Word)kvpage, seL4_AllRights,
				seL4_ARM_Default_VMAttributes);

			if (err) {
				return NULL;
			}
		}

		unsigned int dist_until_page_end = PAGESIZE -
			((ubuf + done) & PAGEMASK);

		unsigned int nbytes = MIN (dist_until_page_end, ksize - done);
		memcpy (kvpage + ((ubuf + done) & PAGEMASK), (void*)(kbuf + done),
			nbytes);

		done += nbytes;
		last_cap = frame_cap;
	}

	err = seL4_ARM_Page_Unmap (last_mapped_cap);
	if (err) {
		return NULL;
	}

	cspace_delete_cap (cur_cspace, last_mapped_cap);
	return kbuf;
}

char* copyin (thread_t thread, vaddr_t ubuf, unsigned int usize, char* kbuf,
	unsigned int ksize) {

	assert (kbuf);
	assert (ksize > 0);
	assert (usize > 0);		/* TODO: copyin_str might pass in 0 when impl */

	if (ksize < usize) {
		return NULL;
	}

	/* obvious null */
	if (ubuf == 0) {
		return NULL;
	}

	int err;

	void* kvpage = (void*)PROCESS_SCRATCH;
	unsigned int done = 0;

	seL4_CPtr last_cap = 0;
	seL4_CPtr last_mapped_cap = 0;

	while (done < usize) {
		seL4_CPtr frame_cap = as_get_page_cap (thread->as, ubuf + done);
		if (!frame_cap) {
			/* no page mapped, ABORT - and free buf? */
			return NULL;
		}

		/* only map in page if it's a new one */
		if (last_cap != frame_cap) {
			if (last_cap) {
				/* unmap old one first */
				err = seL4_ARM_Page_Unmap (last_mapped_cap);
				if (err) {
					return NULL;
				}

				cspace_delete_cap (cur_cspace, last_mapped_cap);
			}

			seL4_CPtr copyinout_cap = cspace_copy_cap (cur_cspace, cur_cspace,
				frame_cap, seL4_AllRights);

			if (!copyinout_cap) {
				return NULL;
			}

			last_mapped_cap = copyinout_cap;

			err = map_page (copyinout_cap, seL4_CapInitThreadPD,
				(seL4_Word)kvpage, seL4_AllRights,
				seL4_ARM_Default_VMAttributes);

			if (err) {
				return NULL;
			}
		}

		unsigned int dist_until_page_end = PAGESIZE - 
			((ubuf + done) & PAGEMASK);

		unsigned int nbytes = MIN (dist_until_page_end, usize - done);
		memcpy ((void*)(kbuf + done), kvpage + ((ubuf + done) & PAGEMASK),
			nbytes);

		done += nbytes;
		last_cap = frame_cap;
	}

	err = seL4_ARM_Page_Unmap (last_mapped_cap);
	if (err) {
		return NULL;
	}

	cspace_delete_cap (cur_cspace, last_mapped_cap);
	return kbuf;
}