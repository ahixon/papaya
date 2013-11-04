#include <stdarg.h>
#include <assert.h>
#include <stdlib.h>
#include <string.h>
//#include <unistd.h>            FIXME: sleep different definition??
#include <sos.h>
#include <pawpaw.h>

#include <sel4/sel4.h>

#define CONSOLE_DEVICE  "/dev/console"
#define NUM_DEFAULT_HANDLES 3
fildes_t handles[NUM_DEFAULT_HANDLES] = {0};

size_t sos_write(void *vData, long int position, size_t count, void *handle) {
    sos_debug_print ("doing sos write\n", strlen("doing sos write\n"));

    unsigned int handleid = (int)handle;
    if (handleid >= NUM_DEFAULT_HANDLES) {
        return 0;
    }

    //if (handleid != STDOUT_FILENO && handleid != STDERR_FILENO) {
    if (handleid != 1 && handleid != 2) {
        return 0;
    }

    if (handles[handleid] <= 0) {
        /* haven't opened yet */
        handles[handleid] = open (CONSOLE_DEVICE, FM_WRITE);
    }

    assert (handles[handleid] > 0);
    sos_debug_print ("sosh: opened actually doing write\n", strlen("sosh: opened actually doing write\n"));
    return write (handles[handleid], vData, count);
}

size_t sos_read(void *vData, long int position, size_t count, void *handle) {
    //implement this to use your syscall
    return 0;
}

void abort(void) {
    pawpaw_suicide ();

    /* ensure this never returns */
    for (;;) { }
}