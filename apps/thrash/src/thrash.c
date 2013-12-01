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

#define THRASH_NUM      896

int main (void) {
    printf ("welcome to thrasher >:)\n");
    printf ("thrashing %d pages (this may take a while)...\n", THRASH_NUM);

    volatile char* locs[THRASH_NUM] = {NULL};

    char lol = 0;
    for (int i = 0; i < THRASH_NUM; i++) {
        //printf ("mallocing %d\n", i);

        volatile char* b = malloc (PAPAYA_IPC_PAGE_SIZE);
        assert (b);
        locs[i] = b;

        for (int j = 0; j < PAPAYA_IPC_PAGE_SIZE; j++) {
            b[j] = lol;
            lol++;
        }
    }

    printf ("checkking results\n");
    lol = 0;
    for (int i = 0; i < THRASH_NUM; i++) {
        //printf ("checking %d\n", i);

        char* b = locs[i];
        for (int j = 0; j < PAPAYA_IPC_PAGE_SIZE; j++) {
            if (b[j] != lol) {
                printf ("failed at %d\n", i);
                assert (b[j] == lol);
            }

            lol++;
        }
    }

    printf ("you survived... this time.\n");

    return 0;
}
