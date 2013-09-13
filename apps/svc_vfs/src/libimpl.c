#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>

#include <sel4/sel4.h>

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