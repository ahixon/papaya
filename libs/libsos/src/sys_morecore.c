/* This implementation of morecore is inspired by the K&R C programming book. */

#include <stdio.h>
#include <stdint.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include "k_r_malloc.h"

#define NALLOC		128

/* freelist defined in malloc.c */
extern Header* _kr_malloc_freep;

/* Increment heap by new_units, and return the start on success, NULL otherwise */
Header*
morecore (unsigned int new_units) {
	void* cp;
	Header* up;
	int rounded_new_units;

	rounded_new_units = NALLOC * ((new_units + NALLOC - 1) / NALLOC);

	cp = sbrk (rounded_new_units * sizeof (Header));
	if (cp == NULL) {
		return NULL;
	}

	up = (Header*)cp;
	up->s.size = rounded_new_units;

	free ((void *)(up + 1));

	return _kr_malloc_freep;

#if 0
    if (morecore_free + new_units
            > (Header*) &morecore_area[MORECORE_AREA_BYTE_SIZE]) {
        /* out of memory */
        return NULL;
    }
    morecore_free->s.size = new_units;
    free((void*) (morecore_free + 1));
    morecore_free += new_units;
    return _kr_malloc_freep;
#endif
}

void *sbrk(intptr_t increment) {
	seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);

    seL4_SetTag(tag);
    seL4_SetMR(0, SYSCALL_SBRK);
    seL4_SetMR(1, increment);
    seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

    return (void*)seL4_GetMR(0);
}