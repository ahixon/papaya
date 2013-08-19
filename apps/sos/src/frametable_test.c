#include <sel4/sel4.h>
#include <assert.h>

#define verbose 5
#include <sys/debug.h>

#include "frametable.h"

void
ft_test1(void)
{
    /* Allocate 10 pages and make sure you can touch them all */
    for (int i = 0; i < 10; i++) {
        /* Allocate a page */
        seL4_Word vaddr;
        frame_alloc(&vaddr);
        assert(vaddr);

        /* Test you can touch the page */
        int* vv = (int*)vaddr;
        printf ("vv points to = %p\n", vv);

        *vv = 0x37;
        assert(*vv == 0x37);

        printf("Page #%d allocated at %p\n",  i, (void *) vaddr);
    }
}
#if 0
void
ft_test2(void)
{
    /* Test that you eventually run out of memory gracefully,
       and doesn't crash */
    for (;;) {
        /* Allocate a page */
        seL4_Word vaddr;
        frame_alloc(&vaddr);
        if (!vaddr) {
        printf("Out of memory!\n");
        break;
        }

        /* Test you can touch the page */
        *vaddr = 0x37;
        assert(*vaddr == 0x37);
    }
}

void
ft_test3(void)
{
    /* Test that you never run out of memory if you always free frames. 
        This loop should never finish */
    for (int i = 0;; i++) {
        /* Allocate a page */
        seL4_Word vaddr;
        page = frame_alloc(&vaddr);
        assert(vaddr != 0);

        /* Test you can touch the page */
        *vaddr = 0x37;
        assert(*vaddr == 0x37);

        printf("Page #%d allocated at %p\n",  i, vaddr);

        frame_free(page);
    }
}

void
ft_testall(void) {
    ft_test1();
    ft_test2();
    ft_test3();
}
#endif