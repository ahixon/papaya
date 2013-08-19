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
        vaddr_t vaddr = frame_alloc();
        assert(vaddr);

        /* Test you can touch the page */
        *(int*)(vaddr) = 0x37;
        assert(*(int*)(vaddr) == 0x37);

        printf("Page #%d allocated at %p\n",  i, (void *) vaddr);
    }
}

void
ft_test2(void)
{
    /* Test that you eventually run out of memory gracefully,
       and doesn't crash */
    for (;;) {
        /* Allocate a page */
        vaddr_t vaddr = frame_alloc();
        if (!vaddr) {
            printf("Out of memory!\n");
            break;
        }

        /* Test you can touch the page */
        int* i = (int*)vaddr;
        *i = 0x37;
        assert(*i == 0x37);
    }
}

void
ft_test3(void)
{
    /* Test that you never run out of memory if you always free frames. 
        This loop should never finish */
    for (int i = 0;; i++) {
        /* Allocate a page */
        vaddr_t vaddr = frame_alloc();
        assert(vaddr != 0);

        /* Test you can touch the page */
        *(int*)vaddr = 0x37;
        assert(*(int*)vaddr == 0x37);

        printf("Page #%d allocated at %p\n",  i, (void*)vaddr);

        frame_free(vaddr);
    }
}