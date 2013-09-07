#include <assert.h>
#include <stdio.h>
#include <stdlib.h>

#include <sel4/sel4.h>

#define NPAGES (128 * 1)

/* called from pt_test */
static void
do_pt_test( int *buf )
{
    printf ("welcome to do_pt_test\n");

    int i;

    /* set */
    for(i = 0; i < NPAGES; i += 4) {
        //printf ("setting %p = %d\n", &buf[i * 1024], i);
        buf[i * 1024] = i;
    }

    //printf ("reading back\n");

    /* check */
    for(i = 0; i < NPAGES; i += 4) {
        //printf ("checking %p = %d\n", &buf[i*1024], i);
        assert(buf[i*1024] == i);
    }
}

static int rec_test = 0;

#define BUF_SIZE ((1 << seL4_PageBits) * NPAGES)

static void
recursive_test (void) {
    int bigbuf[NPAGES * 1024] = {0};
    bigbuf[0] = 1;
    bigbuf[NPAGES * 1024 - 1] = 1;

    //printf ("i am recursive test #%d\n", rec_test);

    rec_test++;
    recursive_test();
    rec_test--;
}

static void
pt_test( void )
{
    printf ("welcome to pt_test (i should be on a big stack now)\n");
    /* need a decent sized stack */
    int buf1[NPAGES * 1024], *buf2 = NULL;

    /* check the stack is above phys mem */
    printf ("checking if we're above physical memory: addr of buf1 = %p, end of buf1 = %p\n", buf1, &buf1[(NPAGES * 1024) - 1]);
    assert((void *) buf1 > (void *) 0x2000000);

    /* stack test */
    printf ("doing stack test...\n");
    do_pt_test(buf1);

    /* heap test */
    printf ("doing heap test (mallocing %d * 1024 bytes)...\n", NPAGES);
    buf2 = malloc(NPAGES * 1024 * sizeof (int));
    assert(buf2);
    printf ("actually running the test\n");
    do_pt_test(buf2);
    printf ("freeing malloc'd buffer\n");
    //free(buf2);

    buf2 = malloc (NPAGES * 1024);
    printf ("buf2 = %p\n", buf2);

    //printf ("OK LET'S GO INSANE AND START HAVING A HUGE STACK???\n");
    //recursive_test();

#if 0
    printf ("stack was at %p\n", &buf1[(NPAGES * 1024) - 1]);
    int i = (NPAGES * 1024) - 1;
    while (1) {
        printf ("setting %p to %d\n", &buf1[i], i);
        buf1[i] = i;
        i++;
    }


    printf ("stack was at %p\n", &buf1[0]);
    int i = 0;
    while (1) {
        printf ("setting %p to %d\n", &buf1[i], i);
        buf1[i] = i;
        i -= 1024;
    }
#endif

/*while (true) {
    printf ("I am sos_test\n");
}*/
}


int main(void){
    printf ("about to start pt_test (so, no big stack yet)\n");
    pt_test();

    printf ("All tests passed. YOU ARE AWESOME!\n");
    return 0;
}
