#include <stdio.h>

#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <sel4/sel4.h>

#include "ttyout.h"

#define ABORT_MSG   ("** That's all there is; there isn't any more. **\n")

static size_t sos_debug_print(const void *vData, long int position, size_t count, void *handle) {
    size_t i;
    const char *realdata = vData;
    for (i = 0; i < count; i++)
        seL4_DebugPutChar(realdata[i]);
    return count;
}

size_t sos_write(void *vData, long int position, size_t count, void *handle) {
    return sos_debug_print (vData, position, count, handle);
}

size_t sos_read(void *vData, long int position, size_t count, void *handle) {
    //implement this to use your syscall
    return 0;
}

void abort(void) {
    sos_debug_print(ABORT_MSG, 0, strlen(ABORT_MSG), 0);
    seL4_DebugHalt();
    while (1)
        ; /* We don't return after this */
}

struct __file __stdin = {
    0,
    sos_read,
    NULL,
    NULL,
    NULL,
    _IONBF,
    NULL,
    0,
    0
};


struct __file __stdout = {
    (void*)1,
    NULL,
    sos_write,
    NULL,
    NULL,
    _IONBF,
    NULL,
    0,
    0
};


struct __file __stderr = {
    (void*)2,
    NULL,
    sos_write,
    NULL,
    NULL,
    _IONBF,
    NULL,
    0,
    0
};

FILE *stdin = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;
