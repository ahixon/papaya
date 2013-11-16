/* This implementation of morecore is inspired by the K&R C programming book. */

#include <stdio.h>
#include <stdint.h>

#include <sel4/sel4.h>
#include <pawpaw.h>
#include <syscalls.h>

#include "k_r_malloc.h"

#define NALLOC		128

/* freelist defined in malloc.c */
extern Header* _kr_malloc_freep;

/* Increment heap by new_units, and return the start on success, NULL otherwise */
Header*
morecore (unsigned int new_units) {
    printf ("** morecore\n");
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
}

void *sbrk(intptr_t increment) {
    /* backup registers since this might be used between syscalls without someone knowing/thinking */
    seL4_Word old_0 = seL4_GetMR (0);
    seL4_Word old_1 = seL4_GetMR (1);

    /* do the syscall */
    printf ("** doing sbrk\n");
    seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_SBRK);
    seL4_SetMR (1, increment);

    seL4_Call (PAPAYA_SYSCALL_SLOT, tag);

    void* addr = (void*)seL4_GetMR(0);

    /* set back old values */
    seL4_SetMR (0, old_0);
    seL4_SetMR (1, old_1);

    return addr;
}