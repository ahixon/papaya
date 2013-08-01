/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 code.
 *      		     Libc will need sos_write & sos_read implemented.
 *
 *      Author:      Ben Leslie
 *
 ****************************************************************************/

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include "ttyout.h"

#include <sel4/sel4.h>

void ttyout_init(void) {
    /* Perform any initialisation you require here */
}

static size_t sos_debug_print(const void *vData, long int position, size_t count, void *handle) {
    size_t i;
    const char *realdata = vData;
    for (i = 0; i < count; i++)
        seL4_DebugPutChar(realdata[i]);
    return count;
}

size_t sos_write(void *vData, long int position, size_t count, void *handle) {
    //implement this to use your syscall
    return sos_debug_print(vData, position, count, handle);
}

size_t sos_read(void *vData, long int position, size_t count, void *handle) {
    //implement this to use your syscall
    return 0;
}

void abort(void) {
    sos_debug_print("sos abort()ed", 0, strlen("sos abort()ed"), 0);
    seL4_DebugHalt();
    while (1)
        ; /* We don't return after this */
}

