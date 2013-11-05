#include <stdio.h>
#include <stdint.h>
#include <string.h>

#include <sel4/sel4.h>

#include <sos.h>

#include <pawpaw.h>
#include <syscalls.h>

#include <vfs.h>
#include <timer.h>

seL4_CNode vfs_ep = 0;
struct pawpaw_share* vfs_share = NULL;
struct pawpaw_share* data_share = NULL;

/* FIXME: what if the VFS service dies and then restarts? endpoint will have changed */
fildes_t open(const char *path, fmode_t mode) {
	seL4_MessageInfo_t msg;

	if (!vfs_ep) {
		vfs_ep = pawpaw_service_lookup ("svc_vfs");
	}

	/* if this is our initial open, create a shared buffer between this thread
	 * and the the VFS service to pass through filenames */
	if (!vfs_share) {
		vfs_share = pawpaw_share_new ();
		if (!vfs_share) {
			sos_debug_print ("failed to make new share\n", strlen("failed to make a new share\n"));
			return -1;
		}
	}

	msg = seL4_MessageInfo_new (0, 0, 1, 3);
    seL4_SetCap (0, vfs_share->cap);

	strcpy (vfs_share->buf, path);
    /*sos_debug_print ("sos: opening ", strlen("sos: opening "));
    sos_debug_print (vfs_share->buf, strlen(vfs_share->buf));
    //sos_debug_print (" on ", 4);
    //sos_debug_print (vfs_share->id + '0', 1);
    sos_debug_print ("\n", 1);*/

    seL4_CPtr recv_cap = pawpaw_cspace_alloc_slot ();
    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, recv_cap, PAPAYA_CSPACE_DEPTH);

    seL4_SetMR (0, VFS_OPEN);
    seL4_SetMR (1, vfs_share->id);
    seL4_SetMR (2, mode);

    seL4_MessageInfo_t reply = seL4_Call (vfs_ep, msg);
    //pawpaw_share_unmount (vfs_share);

    if (seL4_MessageInfo_get_extraCaps (reply) == 1) {
    	return (fildes_t)recv_cap;
    } else {
    	pawpaw_cspace_free_slot (recv_cap);
    	//return seL4_GetMR (0);
        return -1;
    }    
}

int close(fildes_t file) {
    //pawpaw_cspace_free_slot ((seL4_CPtr)file);
	return -1;
}

int read(fildes_t file, char *buf, size_t nbyte) {
	if (nbyte >= (1 << 12)) {
		return -1;		/* FIXME: crappy limitation */
    }

    /* FIXME: in the future, associate locally for FD */
    if (!data_share) {
        data_share = pawpaw_share_new ();
        if (!data_share) {
            return -1;
        }
    }

	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 3);
    pawpaw_share_attach (data_share);

	seL4_SetMR (0, VFS_READ);
    seL4_SetMR (1, data_share->id);
    seL4_SetMR (2, nbyte);

    seL4_Call ((seL4_CPtr)file, msg);
    int read = seL4_GetMR (1);

    if (read > 0) {
        memcpy (buf, data_share->buf, read);
    }

    // unmount needs notifier OR keep onto IDs until all unmounted
    //pawpaw_share_unmount (fd_share);

    return read;
}

int write(fildes_t file, const char *buf, size_t nbyte) {
	if (nbyte >= (1 << 12)) {
        return -1;      /* FIXME: crappy limitation */
    }

    /* FIXME: in the future, associate locally for FD */
    if (!data_share) {
        data_share = pawpaw_share_new ();
        if (!data_share) {
            return -1;
        }
    }

    memcpy (data_share->buf, buf, nbyte);
    /*sos_debug_print ("SOS: writing: '", strlen("SOS: writing: '"));
    sos_debug_print (fd_share->buf, nbyte);
    sos_debug_print ("'\n", 2);*/

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 1, 3);
    pawpaw_share_attach (data_share);
    //seL4_SetCap (0, fd_share->cap);

    seL4_SetMR (0, VFS_WRITE);
    seL4_SetMR (1, data_share->id);
    seL4_SetMR (2, nbyte);

    seL4_Call ((seL4_CPtr)file, msg);
    int wrote = seL4_GetMR (1);

    // unmount needs notifier OR keep onto IDs until all unmounted
    //pawpaw_share_unmount (data_share);

    return wrote;
}

int getdirent(int pos, char *name, size_t nbyte) {
	return -1;
}

int stat(const char *path, stat_t *buf) {
	return -1;
}

/*************** PROCESS SYSCALLS ***************/

pid_t process_create(const char *path) {
#if 0
	return process_create_args (path, NULL, 0);
}

pid_t process_create_args_env (const char* path, const char* argv[], const int argc, const char* envv[], const int envc) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, SYSCALL_PROCESS_CREATE);
    seL4_SetMR (1, (seL4_Word)path);
    seL4_SetMR (2, (seL4_Word)argv);
    char* 
    seL4_SetMR (3, argc + envc)

    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return seL4_GetMR (0);
}

pid_t process_create_args (const char* path, const char* argv[]) {
#endif
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, SYSCALL_PROCESS_CREATE);
    seL4_SetMR (1, (seL4_Word)path);
    seL4_SetMR (2, strlen (path));
    //seL4_SetMR (2, (seL4_Word)argv);
    //seL4_SetMR (3, )

    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return seL4_GetMR (0);
}

int process_delete(pid_t pid) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_PROCESS_DESTROY);
    seL4_SetMR (1, pid);

    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return seL4_GetMR (0);
}

pid_t my_id(void) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, SYSCALL_PROCESS_GETPID);

    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return seL4_GetMR (0);
}

/* sends:
 * vaddr in process address space of processes buffer (should do map-in-out)
 * maximum number to place in buffer
 */
int process_status(process_t *processes, unsigned max) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 3);
    seL4_SetMR (0, SYSCALL_PROCESS_SEND_STATUS);
    seL4_SetMR (1, (seL4_Word)processes);
    seL4_SetMR (2, max);

    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return seL4_GetMR (0);
}

pid_t process_wait(pid_t pid) {
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 2);
    seL4_SetMR (0, SYSCALL_PROCESS_WAIT);
    seL4_SetMR (1, pid);

    seL4_Call (PAPAYA_SYSCALL_SLOT, msg);
    return seL4_GetMR (0);
}

/*************** TIMER SYSCALLS ***************/

int64_t time_stamp (void) {
    return pawpaw_time_stamp ();
}

void sleep (int msec) {
	pawpaw_usleep (msec * 1000);
}

/*************** VM SYSCALLS ***************/

int share_vm(void *adr, size_t size, int writable) {
	return -1;
}