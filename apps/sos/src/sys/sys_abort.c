/* @LICENSE(NICTA_CORE) */

/*
 Author: Philip Derrin
 Created: Wed Jan 25 2006
 */
#include <stdio.h>
#include <sel4/sel4.h>
#include <stdlib.h>
#include "execinfo.h" /*for backtrace()*/
void abort(void) {
    printf("seL4 root server aborted\n");

    /* Printout backtrace*/
    void *array[10] = {NULL};
    int size = 0;

    size = backtrace(array, 10);
    if (size) {

        printf("Backtracing stack PCs:  ");

        for (int i = 0; i < size; i++) {
	        printf("0x%x  ", (unsigned int)array[i]);
	    }
	    printf("\n");
	}


    seL4_DebugHalt();
    while (1)
        ; /* We don't return after this */
}
