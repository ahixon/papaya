#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <cspace/cspace.h>

#include <sos.h>

/* FIXME: should be using libpawpaw for mbox stuff */
seL4_CNode mbox_cap = 8;
char* mbox = (char*)0xa0002000;

int vfs_setup = false;
seL4_CNode vfs_ep = SYSCALL_SERVICE_SLOT;

int
pawpaw_service_lookup (char* name, seL4_CNode cap) {
    seL4_MessageInfo_t msg = seL4_MessageInfo_new(0, 0, 0, 3);
    
    seL4_SetCapReceivePath (4, cap, CSPACE_DEPTH);      /* FIXME: SHOULD NOT BE 4 - SHOULD BE ROOT CSPACE NODE */

    seL4_SetMR(0, SYSCALL_FIND_SERVICE);
    seL4_SetMR(1, (seL4_Word)name);
    seL4_SetMR(2, strlen (name));

    printf ("Looking up service and placing into %d\n", cap);
    seL4_MessageInfo_t reply = seL4_Call (SYSCALL_ENDPOINT_SLOT, msg);
    printf ("Got %d caps\n", seL4_MessageInfo_get_extraCaps (reply));
    return seL4_MessageInfo_get_extraCaps (reply) > 0;
}

fildes_t open(const char *path, fmode_t mode) {
	if (!vfs_setup) {
		/* lookup VFS service first */
		vfs_setup = pawpaw_service_lookup ("sys.vfs", vfs_ep);

		/* VFS service still down? */
		if (!vfs_setup) {
			printf ("Welp, VFS lookup failed\n");
			return -1;
		}
	}

	seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 4);

	seL4_SetCap (0, mbox_cap);
	printf ("OK, copying %s to mbox\n", path);
	strcpy (mbox, path);

	printf ("OK, setting up MRs\n");
    seL4_SetMR (0, VFS_OPEN);
    seL4_SetMR (1, (seL4_Word)mbox);
    seL4_SetMR (2, strlen(mbox));
    seL4_SetMR (3, mode);

    printf ("Calling on %d with mbox cap %d\n", vfs_ep, mbox_cap);
    seL4_Call (vfs_ep, tag);
    printf ("** call done\n");

    return seL4_GetMR(0);
}
/* Open file and return file descriptor, -1 if unsuccessful
 * (too many open files, console already open for reading).
 * A new file should be created if 'path' does not already exist.
 * A failed attempt to open the console for reading (because it is already
 * open) will result in a context switch to reduce the cost of busy waiting
 * for the console.
 * "path" is file name, "mode" is one of O_RDONLY, O_WRONLY, O_RDWR.
 */

// FIXME: needs to cheat with fd = {0, 1, 2}
int close(fildes_t file) {
	return -1;
}

// FIXME: needs to cheat with fd = {0, 1, 2}
int read(fildes_t file, char *buf, size_t nbyte) {
	return -1;
}

// FIXME: needs to cheat with fd = {0, 1, 2}
int write(fildes_t file, const char *buf, size_t nbyte) {
	return -1;
}

int getdirent(int pos, char *name, size_t nbyte) {
	return -1;
}

int stat(const char *path, stat_t *buf) {
	return -1;
}

/* XXX: implement me! easy */
pid_t process_create(const char *path) {
	return -1;
}

/* XXX: implement me! easy */
int process_delete(pid_t pid) {
	return -1;
}

/* XXX: implement me! easy */
pid_t my_id(void) {
	return 0;
}

int process_status(process_t *processes, unsigned max) {
	return -1;
}

pid_t process_wait(pid_t pid) {
	return -1;
}

int64_t time_stamp(void) {
	return 0;
}

void sleep(int msec) {
	return;
}

int share_vm(void *adr, size_t size, int writable) {
	return -1;
}