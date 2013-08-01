/* @LICENSE(NICTA_CORE) */

/* This implementation of morecore is inspired by the K&R C programming book. */
/* Instead of calling sbrk, we use a statically allocated morecore area.      */
/* Implemented by Michael von Tessin, 19 Oct 2010.                            */

#include <stdio.h>
#include "k_r_malloc.h"

/* freelist defined in malloc.c */

extern Header* _kr_malloc_freep;

/* statically allocated morecore area */

#define MORECORE_AREA_BYTE_SIZE 0x100000
#define MORECORE_AREA_SIZE (MORECORE_AREA_BYTE_SIZE / sizeof(long long))
// Declare morecore_area long long so it is doubleword aligned
long long morecore_area[MORECORE_AREA_SIZE];

/* pointer to free space in the morecore area */

Header* morecore_free = (Header*) &morecore_area;

/* actual morecore implementation */

Header *morecore(unsigned int new_units) {
    if (morecore_free + new_units
            > (Header*) &morecore_area[MORECORE_AREA_SIZE]) {
        /* out of memory */
        return NULL;
    }
    morecore_free->s.size = new_units;
    free((void*) (morecore_free + 1));
    morecore_free += new_units;
    return _kr_malloc_freep;
}
