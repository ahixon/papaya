#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sel4/sel4.h>

#include <sos.h>

#include <pawpaw.h>
#include <syscalls.h>

#include <vfs.h>

seL4_CNode vfs_ep = 0;
sbuf_t vfs_buffer = NULL;

/* FIXME: what if the VFS service dies and then restarts? endpoint will have changed */
fildes_t open(const char *path, fmode_t mode) {
	seL4_MessageInfo_t msg;

	while (!vfs_ep) {
		/* lookup VFS service first */
		vfs_ep = pawpaw_service_lookup ("svc_vfs");
		if (!vfs_ep) {
			seL4_Yield();
			//return -1;
		}
	}

	/* if this is our initial open, create a shared buffer between this thread
	 * and the the VFS service to pass through filenames */
	short created = false;
	if (!vfs_buffer) {
		vfs_buffer = pawpaw_sbuf_create (2);
		created = true;
		if (!vfs_buffer) {
			return -1;
		}
	}

	msg = seL4_MessageInfo_new(0, 0, created ? 1 : 0, 3);

	/* if it's the first open, attach our cap */
	if (created) {
		seL4_SetCap (0, pawpaw_sbuf_get_cap (vfs_buffer));
	}

	/* pick a slot to copy to */
	int slot = pawpaw_sbuf_slot_next (vfs_buffer);
	if (slot < 0) {
		/* FIXME: ensure this doesn't happen - do pinning in root server */
		printf ("OPEN: NO MORE SLOTS LEFT???\n");
		return -1;
	}

	printf ("copying in %s to %p\n", path, pawpaw_sbuf_slot_get (vfs_buffer, slot));
	strcpy (pawpaw_sbuf_slot_get (vfs_buffer, slot), path);

    seL4_SetMR (0, VFS_OPEN);
    seL4_SetMR (1, slot);
    seL4_SetMR (2, mode);

    seL4_Call (vfs_ep, msg);

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
	if (nbyte <= (1 << 12))
		return -1;		/* FIXME: crappy limitation */

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