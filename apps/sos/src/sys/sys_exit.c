#include <stdio.h>
#include <stdlib.h>
#include <sel4/sel4.h>

void _Exit(int status) {
    abort();
}
