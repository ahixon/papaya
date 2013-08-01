/* @LICENSE(CUSTOM) */

/* An implementation of malloc as described in the K&R C programming book */

#ifndef _LIBC_K_R_MALLOC_H_
#define _LIBC_K_R_MALLOC_H_

typedef long long Align; /* for alignment to long long boundary */

union header {
    struct {
        union header* ptr; /* next block if on free list */
        unsigned int size; /* size of this block */
    } s;
    Align x; /* force alignment of blocks */
};
typedef union header Header;

Header* morecore(unsigned int new_units);
void free(void* p);

#endif /* _LIBC_K_R_MALLOC_H_ */
