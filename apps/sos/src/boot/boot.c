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

extern char _cpio_archive[];
extern seL4_CPtr rootserver_syscall_cap;

#define BOOT_LIST       "boot.txt"
#define BOOT_LIST_LINE  "\n"
#define BOOT_LIST_SEP   "="

int boot_thread (void) {
    /* find our boot instructions */
    char *cpio_bootlist, *mem_bootlist;
    unsigned long size;

    cpio_bootlist = cpio_get_file (_cpio_archive, BOOT_LIST, &size);
    conditional_panic (!cpio_bootlist, "failed to find boot list '"BOOT_LIST"' in CPIO archive\n");

    /* eugh.. in case we have no new lines at the end - also less chance of mangling CPIO */
    mem_bootlist = malloc (size + 1);
    conditional_panic (!mem_bootlist, "not enough memory to copy in bootlist\n");
    memcpy (mem_bootlist, cpio_bootlist, size);
    *(mem_bootlist + size) = '\0';  /* EOF the string */
    
    /* parse it */
    char* line = strtok (mem_bootlist, BOOT_LIST_LINE);
    while (line != NULL) {
        if (line[0] == '#') {
            /* was a comment, skip */
            line = strtok (NULL, BOOT_LIST_LINE);
            continue;
        }

        char* equals = strpbrk (line, BOOT_LIST_SEP);
        if (!equals) {
            printf ("boot: syntax error in %s: missing '%s' expected, had '%s'\n", BOOT_LIST, BOOT_LIST_SEP, line);
            line = strtok (NULL, BOOT_LIST_LINE);
            continue;
        }

        *equals = '\0';
        char* app = line;
        char* type = equals + 1;

        if (strlen (type) == 0) {
            printf ("boot: missing type for application %s\n", app);
            panic ("missing boot application type");
        }

        if (strcmp (type, "boot") && strcmp (type, "sync") && strcmp (type, "async")) {
            printf ("boot: invalid boot type '%s' for application %s\n", type, app);
            panic ("invalid boot application type");
        }


        printf ("Starting '%s' as %s...\n", app, type);
        thread_t thread = thread_create_from_cpio (app, rootserver_syscall_cap);
        if (!thread) {
            printf ("boot: failed to start '%s' - in CPIO archive?\n", app);
            panic ("failed to start boot application");
        }

        printf ("\tstarted with PID %d\n", thread->pid);

        /* FIXME: wait for applications with 'sync' boot types */

        line = strtok (NULL, BOOT_LIST_LINE);
    }

    free (mem_bootlist);

    return 0;
}