/* @LICENSE(NICTA_CORE) */

/*
 Author: Philip Derrin
 Created: Wed Jan 25 2006
 */

#include <stdio.h>
#include <stdlib.h>
#include <sel4/sel4.h>

void _Exit(int status) {
    abort();
}
