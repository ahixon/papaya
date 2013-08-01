/* @LICENSE(NICTA_CORE) */

/*
  Authors: Kevin Elphinstone
*/

#include <stdio.h> /* For fprintf() */
#include <stdlib.h> /* For abort() */
#include <sel4/sel4.h>
#include "sel4_debug.h"

char *sel4_errlist[] = {
    "seL4_NoError",
    "seL4_InvalidArgument",
    "seL4_InvalidCapability",
    "seL4_IllegalOperation",
    "seL4_RangeError",
    "seL4_AlignmentError",
    "seL4_FailedLookup",
    "seL4_TruncatedMessage",
    "seL4_DeleteFirst",
    "seL4_RevokeFirst",
    "seL4_NotEnoughMemory"
};

void
__sel4_error(int sel4_error, const char *file,
             const char *function, int line, char * str)
{

    fprintf(stderr, "seL4 Error: %s, function %s, file %s, line %d: %s\n",
            sel4_errlist[sel4_error],
            function, file, line, str);
    abort();
}

