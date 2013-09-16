#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>

#include <sos.h>

//struct pawpaw_can* mycan;

extern char _cpio_archive[];

int main(void) {
    printf("Parsing cpio data:\n");
    printf("--------------------------------------------------------\n");
    printf("| index |        name      |  address   | size (bytes) |\n");
    printf("|------------------------------------------------------|\n");
    /*for(i = 0;; i++) {
        unsigned long size;
        const char *name;
        void *data;

        data = cpio_get_entry(_cpio_archive, i, &name, &size);
        if(data != NULL){
            printf("| %3d   | %16s | %p | %12d |\n", i, name, data, size);
        }else{
            break;
        }
    }*/
    printf ("archive at %p\n", _cpio_archive);
    printf("--------------------------------------------------------\n");

    return 0;
}
