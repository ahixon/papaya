#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>
#include <network.h>

#include <sos.h>

#define THRASH_NUM      1024 * 10

int main (void) {
    printf ("welcome to thrasher >:)\n");
    printf ("thrashing %d pages...\n", THRASH_NUM);
    //fflush (stdout);

    volatile char* locs[THRASH_NUM] = {NULL};

    for (int i = 0; i < THRASH_NUM; i++) {
        //printf ("mallocing %d\n", i);
        //fflush (stdout);

        volatile char* b = malloc (PAPAYA_IPC_PAGE_SIZE);
        assert (b);
        locs[i] = b;
        for (int j = 0; j < PAPAYA_IPC_PAGE_SIZE; j++) {
            b[j] = 0xad;
        }
    }

    for (int i = 0; i < THRASH_NUM; i++) {
        //printf ("checking %d\n", i);
        //fflush (stdout);

        char* b = locs[i];
        assert (b[0xa] = 0xad);
    }

    printf ("you survived... this time.\n");

    return 0;
}
