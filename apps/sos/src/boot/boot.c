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
#include <vfs.h>
#include <sos.h>

#include <autoconf.h>

extern char _cpio_archive[];
extern seL4_CPtr rootserver_syscall_cap;

#define BOOT_LIST       "boot.txt"
#define BOOT_LIST_LINE  "\n"
#define BOOT_LIST_SEP   "="
#define BOOT_ARG_SEP    "\t "

#define BOOT_CMD_MOUNT  "mount "
#define BOOT_CMD_SWAP   "swap "

#define BOOT_TYPE_UNKNOWN   0
#define BOOT_TYPE_ASYNC     1
#define BOOT_TYPE_SYNC      2
#define BOOT_TYPE_BOOT      3
#define BOOT_TYPE_CMD_MOUNT 4
#define BOOT_TYPE_CMD_SWAP  5

int mount (char* mountpoint, char* fs);
int parse_fstab (char* path);
int open_swap (char* path);

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

        int type = BOOT_TYPE_UNKNOWN;

        if (strstr (line, BOOT_CMD_MOUNT) == line) {
            /* was mount cmd */
            type = BOOT_TYPE_CMD_MOUNT;
        } else if (strstr (line, BOOT_CMD_SWAP) == line) {
            /* wanted to set swap file */
            type = BOOT_TYPE_CMD_SWAP;
        }

        char* sep_type = BOOT_LIST_SEP;

        if (type == BOOT_TYPE_CMD_MOUNT || type == BOOT_TYPE_CMD_SWAP) {
            sep_type =  BOOT_ARG_SEP;
        }

        char* equals = strpbrk (line, sep_type);
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

        if (type == BOOT_TYPE_CMD_MOUNT) {
            /* mount filesystems */
            if (!parse_fstab (type_str)) {
                printf ("boot: failed parsing fstab '%s'\n", type_str);
                panic ("failed to parse fstab");
            }

        } else if (type == BOOT_TYPE_CMD_SWAP) {
            /* handle setting up swap */
            if (!open_swap (type_str)) {
                printf ("boot: failed to open swap file '%s', swapping will not be available\n", type_str);
                panic ("failed to open swap");  /* XXX: debug, no need to be panic */
            }
        } else {
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
        }

        line = strtok (NULL, BOOT_LIST_LINE);
    }

    free (mem_bootlist);
    return 0;
}

extern struct as_region* share_reg;
extern seL4_CPtr share_badge;
extern seL4_Word share_id;

int mount (char* mountpoint, char* fs) {
    printf ("mounting '%s' on '%s'...\n", fs, mountpoint);

    if (!share_reg) {
        share_reg = create_share_reg (&share_badge, &share_id);
    }

    int mp_len = strlen (mountpoint);
    if (mp_len == 0) {
        return false;
    }

    strcpy (share_reg->vbase, mountpoint);
    *(char*)(share_reg->vbase + mp_len) = '\0';
    strcpy (share_reg->vbase + mp_len + 1, fs);
    
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 2);
    seL4_SetCap (0, share_badge);
    seL4_SetMR (0, VFS_MOUNT);
    seL4_SetMR (1, share_id);

    /* XXX: should get service name from boot list */
    seL4_CPtr service = service_lookup ("svc_vfs");
    if (!service) {
        printf ("failed to find VFS cap\n");
        return false;
    }

    seL4_Call (service, msg);
    return seL4_GetMR (0) == 0;
}

extern seL4_CPtr swap_cap;

int open_swap (char* path) {
    printf ("opening '%s' ...\n", path);

    if (!share_reg) {
        share_reg = create_share_reg (&share_badge, &share_id);
    }

    int mp_len = strlen (path);
    if (mp_len == 0) {
        return false;
    }

    strcpy (share_reg->vbase, path);
    *(char*)(share_reg->vbase + mp_len) = '\0';
    
    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 3);
    seL4_SetCap (0, share_badge);
    seL4_SetMR (0, VFS_OPEN);
    seL4_SetMR (1, share_id);
    seL4_SetMR (2, FM_READ | FM_WRITE);

    /* XXX: should get service name from boot list */
    seL4_CPtr service = service_lookup ("svc_vfs");
    if (!service) {
        printf ("failed to find VFS cap\n");
        return false;
    }

    seL4_CPtr recv_cap = cspace_alloc_slot (cur_cspace);
    assert (recv_cap);
    seL4_SetCapReceivePath (cur_cspace->root_cnode, recv_cap, CSPACE_DEPTH);

    seL4_MessageInfo_t reply = seL4_Call (service, msg);
    if (seL4_MessageInfo_get_extraCaps (reply) != 1) {
        printf ("%s: open failed\n", __FUNCTION__);
        return false;
    }

    swap_cap = recv_cap;
    return seL4_GetMR (0) == 0;
}

int parse_fstab (char* path) {
    char *cpio_fstab, *mem_fstab;
    unsigned long size;
    cpio_fstab = cpio_get_file (_cpio_archive, path, &size);

    if (!cpio_fstab) {
        return false;
    }

    /* eugh.. in case we have no new lines at the end - also less chance of mangling CPIO */
    mem_fstab = malloc (size + 1);
    conditional_panic (!mem_fstab, "not enough memory to copy in fstab\n");
    memcpy (mem_fstab, cpio_fstab, size);
    *(mem_fstab + size) = '\0';  /* EOF the string */
    
    /* parse it */
    int status = true;
    char* line = mem_fstab;
    while (line != NULL) {
        line += strspn (line, BOOT_LIST_LINE);
        if (line[0] == '#') {
            /* was a comment, skip */
            line = strpbrk (line, BOOT_LIST_LINE);
            continue;
        }

        /* FIXME: this is kinda dodgy, really should
         * be using a proper strtok, or at least checking
         * strpbrk results don't go over next line */
        char* fs_type = strpbrk (line, BOOT_ARG_SEP);
        if (!fs_type) {
            status = false;
            break;
        }

        *(fs_type)++ = '\0';

        char* opts = strpbrk (fs_type, BOOT_ARG_SEP);
        if (!opts) {
            status = false;
            break;
        }

        *(opts)++ = '\0';

        char* path = line;

        line = strpbrk (opts, BOOT_LIST_LINE);
        if (line) {
            *(line)++ = '\0';
        }
        
        //printf ("FSTYPE = %s, MOUNTPOINT = %s, OPTS = %s\n", fs_type, path, opts);

        /* try to actually mount it */
        if (!mount (path, fs_type)) {
            status = false;
            break;
        }
    }

    printf ("done loading fstab\n");

    free (mem_fstab);
    return status;
}
