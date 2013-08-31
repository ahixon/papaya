#include <assert.h>
#include "frametable.h"

/*
 * Free frames are stored in a stack, represented in memory by an unrolled doubly linked list
 * segmented across physical frames.
 */

#define MAX_BLOCK_SIZE  (FRAME_SIZE - (2*sizeof (struct freeframe_block*)))

struct freeframe_block {
    struct freeframe_block* prev;
    struct frameinfo* frames[MAX_BLOCK_SIZE];
    struct freeframe_block* next;
};

struct {
    struct freeframe_block* root;
    struct freeframe_block* current;
    int total_count;
    int block_index;
} freeframes;

void freeframe_init (void) {
    freeframes.current = freeframes.root;
    freeframes.total_count = 0;
    freeframes.block_index = 0;

    //frame_alloc (&freeframes.root);
}

void freeframe_push (struct frameinfo* b) {
    freeframes.total_count++;
    freeframes.block_index++;

    if (freeframes.block_index < MAX_BLOCK_SIZE) {
        freeframes.current->frames[freeframes.block_index] = b;
    } else {
        freeframes.block_index = 0;

        //frame_alloc (&freeframes.current->next);
        /* FIXME: what happens on OOM */

        struct freeframe_block* new = freeframes.current->next;
        new->next = NULL;
        new->prev = freeframes.current;
        new->frames[0] = b;

        freeframes.current = new;
    }
}

struct frameinfo* freeframe_pop (void) {
    if (freeframes.total_count == 0) {
        return NULL;
    }

    struct frameinfo* ret = freeframes.current->frames[freeframes.block_index];
    freeframes.block_index--;
    freeframes.total_count--;

    if (freeframes.block_index < 0) {
        struct freeframe_block* oldcurrent = freeframes.current;
        assert (oldcurrent->prev != NULL);      /* otherwise we'll free the root */

        freeframes.block_index = MAX_BLOCK_SIZE - 1;
        freeframes.current = freeframes.current->prev;
        freeframes.current->next = NULL;

        //frame_free (oldcurrent);
    }

    return ret;
}