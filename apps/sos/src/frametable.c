#include <sel4/sel4.h>

#include <cspace/cspace.h>

#include "ut_manager/ut.h"

#include "frametable.h"
#include "freeframe.h"

#define verbose 5
#include <sys/debug.h>

void
frametable_init (void)
{
    /* create free frame stack */
    freeframe_init ();

    /* create allocated frametable */

}

seL4_Word
frame_alloc (seL4_Word* vaddr)
{
    seL4_Word frame_addr;
    seL4_CPtr frame_cap;
    int err;

    /* first, try to get a previously allocated, but now free, frame */
    //struct frameinfo* head = freeframe_pop();
    /*if (head) {
        head->flags &= ~FRAME_FREE;

        *vaddr = head->paddr;
        return head->paddr;
    }*/

    /* didn't find anything, so reserve some from untyped memory */
    frame_addr = ut_alloc(seL4_PageBits);
    if (!frame_addr) {
        /* oops, out of memory! */
        return 0;
    }
    
    /* awesome; we have memory, now retype it so we get a vaddr */
    err =  cspace_ut_retype_addr(frame_addr,
                                 seL4_ARM_SmallPageObject,
                                 seL4_PageBits,
                                 cur_cspace,
                                 &frame_cap);
    if (err) {
        return 0;
    }

    printf ("paddr = %p\n", (void*)frame_addr);
    printf ("vaddr = %p\n", (void*)frame_cap);

    //*vaddr = frame_cap;
    *vaddr = frame_addr;
    
    return frame_addr;
}

void
frame_free (vaddr_t vaddr) {
    /* lookup frame in our two level frametable */

    /* got it, mark it as free'd */
    freeframe_push ((struct frameinfo*)vaddr);
}