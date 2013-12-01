#include <sel4/sel4.h>
#include <sos.h>

#include <stdlib.h>
#include <stdio.h>

#include <stdint.h>
#include <string.h>

#include <pawpaw.h>
#include <syscalls.h>

#include <vfs.h>
#include <timer.h>

seL4_CNode vfs_ep = 0;

struct fhandle {
    struct pawpaw_share* share;
    fildes_t fd;
    seL4_CPtr cap;

    struct fhandle* next;
};

fildes_t last_fd = 0;          /* FIXME: should be bitmap */
struct fhandle* open_list = NULL;    /* FIXME: should be hashmap */

struct fhandle* lookup_fhandle (fildes_t file);

/* FIXME: what if the VFS service dies and then restarts? endpoint will have changed */
fildes_t open(const char *path, fmode_t mode) {
    seL4_MessageInfo_t msg;

	if (!vfs_ep) {
		vfs_ep = pawpaw_service_lookup ("svc_vfs");
	}

	struct pawpaw_share* share = pawpaw_share_new ();
	if (!share) {
		return -1;
	}

    struct fhandle* h = malloc (sizeof (struct fhandle));
    if (!h) {
        pawpaw_share_unmount (share);
        return -1;
    }

	msg = seL4_MessageInfo_new (0, 0, 1, 3);

    seL4_CPtr recv_cap = pawpaw_cspace_alloc_slot ();
    if (!recv_cap) {
        pawpaw_share_unmount (share);
        free (h);
        return -1;
    }

    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, recv_cap, PAPAYA_CSPACE_DEPTH);

    seL4_SetCap (0, share->cap);
	strcpy (share->buf, path);
    seL4_SetMR (0, VFS_OPEN);
    seL4_SetMR (1, share->id);
    seL4_SetMR (2, mode);

    seL4_MessageInfo_t reply = seL4_Call (vfs_ep, msg);

    if (seL4_MessageInfo_get_extraCaps (reply) >= 1) {
        h->share = share;
        h->fd = last_fd;
        last_fd++;
        h->cap = recv_cap;

        /* add to list */
        h->next = open_list;
        open_list = h;

        return h->fd;
    } else {
        free (h);
        pawpaw_share_unmount (share);
    	pawpaw_cspace_free_slot (recv_cap);
    	return seL4_GetMR (0);
    }
}

int close(fildes_t file) {
    struct fhandle* fh = lookup_fhandle (file);
    if (!fh) {
        return -1;
    }

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, VFS_CLOSE);
    seL4_Call (fh->cap, msg);
    if (seL4_GetMR (0) == 0) {
        //pawpaw_cspace_free_slot (fh->cap);
        /* should DELETE the slot, then free */
        //pawpaw_share_unmount (fh->share);

        /* FIXME: fix up list */
        //free (fh);
        return 0;
    } else {
	   return -1;
    }
}

struct fhandle* lookup_fhandle (fildes_t file) {
    struct fhandle* h = open_list;
    while (h) {
        if (h->fd == file) {
            return h;
        }

        h = h->next;
    }

    return NULL;
}

int read (fildes_t file, char *buf, size_t nbyte) {
	if (nbyte >= (1 << 12)) {
		return -1;		/* FIXME: crappy limitation */
    }

    struct fhandle* fh = lookup_fhandle (file);
    if (!fh) {
        return -1;
    }
    
	seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, pawpaw_share_attach (fh->share), 3);
    //seL4_SetCap (0, fh->share->cap);
    
	seL4_SetMR (0, VFS_READ);
    seL4_SetMR (1, fh->share->id);
    seL4_SetMR (2, nbyte);

    seL4_Call (fh->cap, msg);
    int read = seL4_GetMR (0);

    if (read > 0) {
        memcpy (buf, fh->share->buf, read);
    }

    return read;
}

int write(fildes_t file, const char *buf, size_t nbyte) {
	if (nbyte >= (1 << 12)) {
        return -1;      /* FIXME: crappy limitation */
    }

    struct fhandle* fh = lookup_fhandle (file);
    if (!fh) {
        return -1;
    }

    memcpy (fh->share->buf, buf, nbyte);

    seL4_MessageInfo_t msg = seL4_MessageInfo_new (0, 0, pawpaw_share_attach (fh->share), 3);
    //seL4_SetCap (0, fh->share->cap);

    seL4_SetMR (0, VFS_WRITE);
    seL4_SetMR (1, fh->share->id);
    seL4_SetMR (2, nbyte);

    seL4_Call (fh->cap, msg);
    int wrote = seL4_GetMR (0);

    return wrote;
}

int getdirent (int pos, char *name, size_t nbyte) {
	seL4_MessageInfo_t msg;

    if (!vfs_ep) {
        vfs_ep = pawpaw_service_lookup ("svc_vfs");
    }

    struct pawpaw_share* share = pawpaw_share_new ();
    if (!share) {
        return -1;
    }

    msg = seL4_MessageInfo_new (0, 0, 1, 4);

    seL4_CPtr recv_cap = pawpaw_cspace_alloc_slot ();
    if (!recv_cap) {
        pawpaw_share_unmount (share);
        return -1;
    }

    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, recv_cap, PAPAYA_CSPACE_DEPTH);

    seL4_SetCap (0, share->cap);
    strcpy (share->buf, name);
    seL4_SetMR (0, VFS_LISTDIR);
    seL4_SetMR (1, share->id);
    seL4_SetMR (2, pos);
    seL4_SetMR (3, nbyte);

    seL4_Call (vfs_ep, msg);
    int read = seL4_GetMR (0);
    if (read >= 0) {
        memcpy (name, share->buf, nbyte);
        pawpaw_share_unmount (share);
        return read;
    } else {
        pawpaw_share_unmount (share);
        return -1;
    }
}

int stat (const char *path, stat_t *buf) {
	seL4_MessageInfo_t msg;

    if (!vfs_ep) {
        vfs_ep = pawpaw_service_lookup ("svc_vfs");
    }

    struct pawpaw_share* share = pawpaw_share_new ();
    if (!share) {
        return -1;
    }

    memcpy (share->buf, path, strlen (path));

    msg = seL4_MessageInfo_new (0, 0, 1, 2);

    seL4_CPtr recv_cap = pawpaw_cspace_alloc_slot ();
    if (!recv_cap) {
        pawpaw_share_unmount (share);
        return -1;
    }

    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, recv_cap, PAPAYA_CSPACE_DEPTH);

    seL4_SetCap (0, share->cap);
    strcpy (share->buf, path);
    seL4_SetMR (0, VFS_STAT);
    seL4_SetMR (1, share->id);

    seL4_Call (vfs_ep, msg);
    int status = seL4_GetMR (0);
    if (status == 0) {
        memcpy (buf, share->buf, sizeof (stat_t));
    }

    pawpaw_share_unmount (share);
    return status;
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