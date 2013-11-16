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

#include <assert.h>
#include <vfs.h>

#include <cpio/cpio.h>

seL4_CPtr service_ep = PAPAYA_INITIAL_FREE_SLOT;
extern char _cpio_archive[];

static int vfs_open (struct pawpaw_event* evt);
static int vfs_read (struct pawpaw_event* evt);
static int vfs_read_offset (struct pawpaw_event* evt);
static int vfs_register_cap (struct pawpaw_event* evt);
//int fs_cpio_close (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
	{   0,  0,  0   },      //  fs register info
    {   vfs_register_cap,  	0,  HANDLER_REPLY   					},	/* for async cap registration */
    {   0,  0,  0   },      //  mount
    {   vfs_open,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   vfs_read,			2,  HANDLER_REPLY | HANDLER_AUTOMOUNT	},
    {   0,  0,  0   },      //  write
    {   0,  0,  0   },      //  close
    {   0,  0,  0   },      //  listdir{   vfs_listdir,        3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   0,  0,  0   },      //  stat{   vfs_stat,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   }
    {   vfs_read_offset,	6,  HANDLER_AUTOMOUNT	},
};

struct pawpaw_event_table handler_table = {
	VFS_NUM_EVENTS, handlers, "fs_cpio"
};

static seL4_CPtr cap = 0;
static int vfs_register_cap (struct pawpaw_event* evt) {
	printf ("got register\n");
	/* FIXME: regsiter cap, badge pair and lookup with 2nd argument of
	 * async open */

	assert (seL4_MessageInfo_get_extraCaps (evt->msg) == 1);
	cap = pawpaw_event_get_recv_cap ();

	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
	seL4_SetMR (0, 1);	/* FIXME: this should be new ID */
	return PAWPAW_EVENT_NEEDS_REPLY;
}

static seL4_CPtr lookup_cap (struct pawpaw_event* evt) {
	return cap;
}

static int vfs_open (struct pawpaw_event* evt) {
	assert (evt->share);

	/* FIXME: check mode + don't die on no share */

	unsigned long size;
    char *name;
    for (int i = 0; cpio_get_entry (
    	_cpio_archive, i, (const char**)&name, &size); i++) {

    	if (strcmp (evt->share->buf, name) == 0) {
    		seL4_CPtr their_cap = pawpaw_cspace_alloc_slot ();
    		if (!their_cap) {
    			printf ("cpio: failed to alloc slot\n");
    			break;
    		}

		    int err = seL4_CNode_Mint (
		        PAPAYA_ROOT_CNODE_SLOT, their_cap,  PAPAYA_CSPACE_DEPTH,
		        PAPAYA_ROOT_CNODE_SLOT, service_ep, PAPAYA_CSPACE_DEPTH,
		        seL4_AllRights, seL4_CapData_Badge_new (i));

		    if (err) {
		    	printf ("cpio: failed to mint cap\n");
		    	break;
		    }

		    printf ("cpio: found file '%s' OK, cap in %d\n", name, their_cap);
		    evt->reply = seL4_MessageInfo_new (0, 0, 1, 1);
    		seL4_SetCap (0, their_cap);
			seL4_SetMR (0, 0);

			return PAWPAW_EVENT_NEEDS_REPLY;
    	}
    }

    printf ("cpio: no such file '%s'\n", evt->share->buf);
	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
	seL4_SetMR (0, -1);
	return PAWPAW_EVENT_NEEDS_REPLY;
}

/* FIXME: what about file handle offsets */
static int vfs_read (struct pawpaw_event* evt) {
	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

	if (!evt->share) {
		seL4_SetMR (0, -1);
		return PAWPAW_EVENT_NEEDS_REPLY;
	}

	unsigned long size;
    char *name;
    char *file = cpio_get_entry (
    	_cpio_archive, evt->badge, (const char**)&name, &size);

	if (file) {
		printf ("found entry for badge %d\n", evt->badge);
		seL4_Word amount = evt->args[0];
		if (amount > size) {
			amount = size;
		}

		memcpy (evt->share->buf, file, amount);
		seL4_SetMR (0, amount);
	} else {
		printf ("failed to find entry %d\n", evt->badge);
		seL4_SetMR (0, -1);
	}

	return PAWPAW_EVENT_NEEDS_REPLY;
}

static int vfs_read_offset (struct pawpaw_event* evt) {

	/* FIXME: should pass through to another func */
	printf ("cpio: hello from read_offset\n");

	seL4_CPtr old_reply = evt->reply_cap;
	evt->reply = seL4_MessageInfo_new (0, 0, 0, 2);

	if (evt->args[3]) {
		//printf ("cpio: waiting for callback cap..\n");
		//seL4_ReplyWait (service_ep, NULL);

		/* FIXME: replywait inside pawpaw would be nice? if it makes
		sure it's the same EP? */

		//seL4_Send (evt->reply_cap, seL4_MessageInfo_new (0, 0, 0, 0));
		//seL4_Wait (service_ep, NULL);
		//printf ("Got it...\n");

		evt->reply_cap = lookup_cap (evt);	/* FIXME: use arg[3] */
		if (evt->reply_cap == 0) {
			printf ("cpio: reply cap not registered\n");
			seL4_SetMR (0, -1);
			evt->reply_cap = old_reply;
			return PAWPAW_EVENT_NEEDS_REPLY;
		}
	}

	if (!evt->share) {
		seL4_SetMR (0, -1);
		printf ("cpio: share not found\n");
		evt->reply_cap = old_reply;
		return PAWPAW_EVENT_NEEDS_REPLY;
	}

	/* FIXME: should go back into SOS - do we even need this? */
	memset (evt->share->buf, 0, 0x1000);

	unsigned long size;
    char *name;
    char *file = cpio_get_entry (
    	_cpio_archive, evt->badge, (const char**)&name, &size);

	if (file) {
		char* end = file + size;
		char* loc = file + evt->args[1];
		seL4_Word amount = evt->args[0];
		if (loc >= end) {
			seL4_SetMR (0, -1);
		} else {
			seL4_Word remaining = end - loc;
			if (amount > remaining) {
				amount = remaining;
			}


			seL4_Word buf_offset = evt->args[2];
			if (buf_offset > PAPAYA_IPC_PAGE_SIZE) {
				buf_offset = PAPAYA_IPC_PAGE_SIZE;
			}

			if (buf_offset + amount > PAPAYA_IPC_PAGE_SIZE) {
				amount = PAPAYA_IPC_PAGE_SIZE - buf_offset;
			}

			printf ("file base = 0x%x\n", file);
			printf ("loading from = 0x%x (offset = 0x%x, len = 0x%x, buf_offset = 0x%x)\n", loc, loc - file, amount, buf_offset);

			memcpy (evt->share->buf + buf_offset, loc, amount);
			seL4_SetMR (0, amount);
		}
	} else {
		printf ("failed to find entry %d\n", evt->badge);
		seL4_SetMR (0, -1);
	}

	/* send out reply */
	seL4_SetMR (1, evt->args[4]);			/* client evt id */
	printf ("Sending reply with 0x%x\n", evt->args[4]);
	seL4_Send (evt->reply_cap, evt->reply);
	printf ("Done\n");
	evt->reply_cap = old_reply;

	/* spend it */
	//cspace_delete_cap (cur_cspace, evt->reply_cap);
	//seL4_Notify (evt->reply_cap, 0);
	return PAWPAW_EVENT_HANDLED;
}


/*
 * CPIO filesystem
 */
int fs_cpio_main (void) {
	printf ("fs_cpio: starting\n");	
    pawpaw_event_init ();

    printf ("Parsing cpio data:\n");
    printf ("--------------------------------------------------------\n");
    printf ("| index |        name      |  address   | size (bytes) |\n");
    printf ("|------------------------------------------------------|\n");
    for (int i = 0;; i++) {
        unsigned long size;
        const char *name;
        void *data;

        data = cpio_get_entry (_cpio_archive, i, &name, &size);
        if (data != NULL) {
            printf ("| %3d   | %16s | %p | %12d |\n", i, name, data, size);
        } else {
            break;
        }
    }
    printf ("archive at %p\n", _cpio_archive);
    printf ("--------------------------------------------------------\n");

    printf ("fs_cpio: started\n");
	pawpaw_event_loop (&handler_table, NULL, service_ep);
	return 0;
}