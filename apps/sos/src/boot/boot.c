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

#define BOOT_TYPE_UNKNOWN   0
#define BOOT_TYPE_ASYNC     1
#define BOOT_TYPE_SYNC      2
#define BOOT_TYPE_BOOT      4

/* XXX: double rainbow^H^H^Hhack. can't get timer EP, nor should sleep */
void sleep (int msec) {
    for (int i = 0; i < msec * 100; i++) {
        seL4_Yield();
    }
}

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
        char* type_str = equals + 1;

        if (strlen (type_str) == 0) {
            printf ("boot: missing type for application %s\n", app);
            panic ("missing boot application type");
        }

        int type = BOOT_TYPE_UNKNOWN;
        if (strcmp (type_str, "async") == 0) {
            type = BOOT_TYPE_ASYNC;
        } else if (strcmp (type_str, "boot") == 0) {
            type = BOOT_TYPE_BOOT;
        } else if (strcmp (type_str, "sync") == 0) {
            type = BOOT_TYPE_SYNC;
        } 

        if (type == BOOT_TYPE_UNKNOWN) {
            printf ("boot: invalid boot type '%s' for application %s\n", type_str, app);
            panic ("invalid boot application type");
        }

        /* XXX: sleep a little first */
        if (type == BOOT_TYPE_BOOT) {
            printf ("boot app\n");
            sleep (3000);
        }

        printf ("Starting '%s' as %s...\n", app, type_str);
        thread_t thread = thread_create_from_cpio (app, rootserver_syscall_cap);
        if (!thread) {
            printf ("boot: failed to start '%s' - in CPIO archive?\n", app);
            if (type != BOOT_TYPE_BOOT) {
                /* only panic if regular boot service */
                panic ("failed to start boot application");
            }
        } else {
            printf ("\tstarted with PID %d\n", thread->pid);

            /* XXX: should wait for EP rather than sleeping */
            if (type == BOOT_TYPE_SYNC) {
                printf ("sync app\n");
                sleep (3000);
            } else if (type == BOOT_TYPE_BOOT && thread) {
                printf ("boot: boot thread started, exiting bootsvc\n");
                break;
            }
        }

        line = strtok (NULL, BOOT_LIST_LINE);
    }

    free (mem_bootlist);
    return 0;
}