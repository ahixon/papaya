/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include <sel4/sel4.h>

static size_t sel4_write(void *data, long int position, size_t count,
        void *handle /*unused*/) {
    size_t i;
    char *realdata = data;
    for (i = 0; i < count; i++) {
        seL4_DebugPutChar(realdata[i]);
    }
    return count;
}

static size_t sel4_read(void *data, long int position, size_t count,
        void *handle /*unused*/) {
    /*
     root server can't read input
     */
    return 0;
}

struct __file __stdin =
        { NULL, sel4_read, NULL, NULL, NULL, _IONBF, NULL, 0, 0 };

struct __file __stdout = { NULL, NULL, sel4_write, NULL, NULL, _IONBF, NULL, 0,
        0 };

struct __file __stderr = { NULL, NULL, sel4_write, NULL, NULL, _IONBF, NULL, 0,
        0 };

FILE *stdin = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;
