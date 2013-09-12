#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <sos.h>
#include <pawpaw.h>

/* FIXME: should be using libpawpaw for mbox stuff */
seL4_CNode mbox_cap = 8;
char* mbox = (char*)0xa0002000;

seL4_CNode vfs_ep = 0;

fildes_t open(const char *path, fmode_t mode) {
	if (!vfs_ep) {
		/* lookup VFS service first */
		vfs_ep = pawpaw_service_lookup ("sys.vfs");

		/* VFS service still down? */
		if (!vfs_ep) {
			printf ("Welp, VFS lookup failed\n");
			return -1;
		}
	}

	seL4_MessageInfo_t tag = seL4_MessageInfo_new(0, 0, 1, 4);

	seL4_SetCap (0, mbox_cap);
	strcpy (mbox, path);

    seL4_SetMR (0, VFS_OPEN);
    seL4_SetMR (1, (seL4_Word)mbox);
    seL4_SetMR (2, strlen(mbox));
    seL4_SetMR (3, mode);

    seL4_Call (vfs_ep, tag);

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