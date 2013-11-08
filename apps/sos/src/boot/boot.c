#include <stdlib.h>
#include <string.h>

#include <sel4/sel4.h>
#include <thread.h>
#include <cspace/cspace.h>
#include <pawpaw.h>

#include <vm/vm.h>
#include <vm/addrspace.h>

#include "ut_manager/ut.h"

#include <elf/elf.h>
#include "elf.h"

#include "vm/vmem_layout.h"

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>
#include <assert.h>

#include <cpio/cpio.h>

#include <autoconf.h>

extern char _cpio_archive[];        /* FIXME: move this out of here, one day.. */
extern seL4_CPtr rootserver_syscall_cap;

#define BOOT_LIST       "boot.txt"
#define BOOT_LIST_SEP   "\n"

int boot_thread (void) {
    /* find our boot instructions */
    char* bootlist = NULL;
    unsigned long size;

    bootlist = cpio_get_file (_cpio_archive, BOOT_LIST, &size);
    conditional_panic (!bootlist, "failed to find boot list in CPIO archive\n");
    
    /* parse it */
    char* line = strtok (bootlist, BOOT_LIST_SEP);
    int linenum = 1;
    while (line != NULL) {
        if (line[0] == '#') {
            /* was a comment, skip */
            line = strtok (NULL, BOOT_LIST_SEP);
            linenum++;
            continue;
        }

        char* equals = strpbrk (line, "="BOOT_LIST_SEP);
        if (*equals != '=') {
            printf ("boot: syntax error on line %d of %s: missing '=' expected\n", linenum, BOOT_LIST);
            //panic ("boot file syntax error");
            line = strtok (NULL, BOOT_LIST_SEP);
            linenum++;
            continue;
        }

        char* type = equals + 1;

        *equals = '\0';
        char* app = line;

        printf ("Starting '%s' as %s...\n", app, type);
        thread_t thread = thread_create_from_cpio (app, rootserver_syscall_cap);
        if (!thread) {
            printf ("boot: failed to start '%s' - in CPIO archive?\n", app);
            panic ("failed to start boot application");
        }

        printf ("\tstarted with PID %d\n", thread->pid);

        line = strtok (NULL, BOOT_LIST_SEP);
        linenum++;
    }
}