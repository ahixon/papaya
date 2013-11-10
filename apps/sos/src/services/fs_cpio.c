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
//int fs_cpio_close (struct pawpaw_event* evt);

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
	{   0,  0,  0   },      //  fs register info
    {   0,  0,  0   },      //  fs register cap
    {   0,  0,  0   },      //  mount
    {   vfs_open,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   vfs_read,			2,  HANDLER_REPLY | HANDLER_AUTOMOUNT	},
    {   0,  0,  0   },      //  write
    {   0,  0,  0   },      //  close
    {   0,  0,  0   },      //  listdir{   vfs_listdir,        3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   0,  0,  0   },      //  stat{   vfs_stat,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   }
    {   vfs_read_offset,	4,  HANDLER_REPLY | HANDLER_AUTOMOUNT	},
    //{	vfs_set_cap,		}
};

struct pawpaw_event_table handler_table = {
	VFS_NUM_EVENTS, handlers, "fs_cpio"
};

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

	if (evt->args[2]) {
		printf ("cpio: waiting for callback cap..\n");
		//seL4_ReplyWait (service_ep, NULL);

		/* FIXME: replywait inside pawpaw would be nice? if it makes
		sure it's the same EP? */

		seL4_Send (evt->reply_cap, seL4_MessageInfo_new (0, 0, 0, 0));
		seL4_Wait (service_ep, NULL);
		//printf ("Got it...\n");

		evt->reply_cap = pawpaw_event_get_recv_cap ();
		//pawpaw_event_get_recv_cap ();
	}

	evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);

	if (!evt->share) {
		seL4_SetMR (0, -1);
		printf ("cpio: share not found\n");
		return PAWPAW_EVENT_NEEDS_REPLY;
	}

	/* FIXME: should go back into SOS */
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

			printf ("file base = 0x%x\n", file);
			printf ("loading from = 0x%x (offset = 0x%x)\n", loc, loc - file);

			memcpy (evt->share->buf, loc - 0x680, amount);
			seL4_SetMR (0, amount);
		}
	} else {
		printf ("failed to find entry %d\n", evt->badge);
		seL4_SetMR (0, -1);
	}

	seL4_Send (evt->reply_cap, evt->reply);
	evt->reply_cap = old_reply;

	return PAWPAW_EVENT_HANDLED;
}


/*
 * CPIO filesystem
 */
int fs_cpio_main (void) {
	printf ("fs_cpio: starting\n");	
    pawpaw_event_init ();

    printf ("fs_cpio: started\n");
	pawpaw_event_loop (&handler_table, NULL, service_ep);
	return 0;
}