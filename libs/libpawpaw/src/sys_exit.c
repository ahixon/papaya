/* @LICENSE(NICTA_CORE) */

/*
 Author: Philip Derrin
 Created: Wed Jan 25 2006
 */

#include <stdio.h>
#include <sel4/sel4.h>

#include "ttyout.h"

void _Exit(int status) {
    abort();
    /* we shouldn't come back */
    while (1)
        ;
}
