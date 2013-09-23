#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <stdio.h>

seL4_MessageInfo_t syscall_suicide (thread_t thread) {
    /* FIXME: should actually destroy thread + resources instead of not returning */
    printf ("\n!!! thread %s wanted to die - R U OK? !!!\n", thread->name);

    /* FIXME: yuck */
    return seL4_MessageInfo_new (0, 0, 0, 0);
}