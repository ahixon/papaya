#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <sel4/sel4.h>

#define NPAGES 128

/* called from pt_test */
static void
do_pt_test( char *buf )
{
    int i;

    /* set */
    for(i = 0; i < NPAGES; i += 4)
    buf[i * 1024] = i;

    /* check */
    for(i = 0; i < NPAGES; i += 4)
    assert(buf[i*1024] == i);
}

static void
pt_test( void )
{
    printf ("welcome to pt_test (i should be on a big stack now)\n");
    /* need a decent sized stack */
    char buf1[NPAGES * 1024], *buf2 = NULL;

    /* check the stack is above phys mem */
    printf ("checking if we're above physical memory: addr of buf1 = %p\n", buf1);
    assert((void *) buf1 > (void *) 0x2000000);

    /* stack test */
    printf ("doing stack test...\n");
    do_pt_test(buf1);

    /* heap test */
    printf ("doing heap test (mallocing %d * 1024 bytes)...\n", NPAGES);
    buf2 = malloc(NPAGES * 1024);
    assert(buf2);
    printf ("actually running the test\n");
    do_pt_test(buf2);
    printf ("freeing malloc'd buffer\n");
    free(buf2);
}

int main(void){
    printf ("about to start pt_test (so, no big stack yet)\n");
    pt_test();

    printf ("All tests passed. YOU ARE AWESOME!\n");
    return 0;
}
