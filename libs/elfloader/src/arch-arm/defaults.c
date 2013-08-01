/* @LICENSE(NICTA_CORE) */

/* Default implementations of required utility functions. Override these under
 * plat-* if there is a more appropriate implementation for a given platform.
 */

#include "stdio.h"

void
__attribute__((weak)) abort(void)
{
    printf("abort() called.\n");

    while (1);
}

#ifdef THREAD_SAFE

#include <mutex/mutex.h>

void
__attribute__((weak)) mutex_count_lock(struct mutex *m)
{
    return;
}

void
__attribute__((weak)) mutex_count_unlock(struct mutex *m)
{
    return;
}

#endif /* THREAD_SAFE */

