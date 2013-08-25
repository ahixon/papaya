/****************************************************************************
 *
 *      $Id:  $
 *
 *      Description: Simple milestone 0 code.
 *                   Libc will need sos_write & sos_read implemented.
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
#include <syscalls.h>

void ttyout_init(void) {
    /* Run a quick test */
    char* test = "hello world from init\n";
    sos_write (test, 0, strlen (test), NULL);
}

static size_t sos_debug_print(const void *vData, long int position, size_t count, void *handle) {
    size_t i;
    const char *realdata = vData;
    for (i = 0; i < count; i++)
        seL4_DebugPutChar(realdata[i]);
    return count;
}

size_t sos_write(void *vData, long int position, size_t count, void *handle) {
    size_t totalsent = 0;

    while (count > 0) {
        int size = count;
        if (size > seL4_MsgMaxLength - 1) {
            size = seL4_MsgMaxLength - 1;
        }

        seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 0, size + 1);
        seL4_SetTag(tag);

        seL4_SetMR(0, SYSCALL_NETWRITE);

        /* load up the data (what is the point of "position"?!) */
        const char *realdata = ((const char*)vData) + totalsent;
        for (int i = 0; i < size; i++) {
            seL4_SetMR (i + 1, realdata[i]);
        }

        seL4_Call(SYSCALL_ENDPOINT_SLOT, tag);

        int sent = seL4_GetMR(0);
        
        /* abort if we got non-zero length back (ie serial driver failed badly) */
        if (sent < 0) {
            return totalsent;
        }

        count -= sent;
        totalsent += sent;
    }

    return totalsent;
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

