#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>

#include <sos.h>
#include <vfs.h>

/* some shit trie thing */
struct fs_node {
    char* dirname;

    seL4_CPtr fs_ep_cap;
    seL4_Word badge;
    seL4_Word buf_id;
    struct fs_node* children;

    struct fs_node* level_next;
    struct fs_node* mounted_next;
};

struct fs_node* node_head; /* head for iterating */
struct fs_node* root;    /* ROOT NODE for walking trie */

struct filesystem {
    char* type;
    seL4_Word owner_badge;
    seL4_CPtr cap;
    struct filesystem* next;
};

struct filesystem* fs_head = NULL;

char* get_next_path_part (char* s, char* end) {
    char* c = s;
    while (c < end) {
        if (*c == '/') {
            *c = '\0';

            return c + 1;
        }

        c++;
    }

    return NULL;
}

int parse_filename (char* fname, struct fs_node* parent, struct fs_node** dest_fs, char** remaining) {
    /* check cache for this filename
     * if we find it, tell filesystem to "open file with inode" and the one they gave us when the said cache it
     * if we find reverse cache, tell user straight away 
     * otherwise, loop through all filesystems, asking them if they have this file

     * FIXME: NEED TO DECIDE if cache works on fsid, then remaining path, or full path??

     */

    /* walk mounted filesystems thing */

    struct fs_node* fs = parent;
    struct fs_node* found = NULL;
    char* cur = fname;
    char* end = cur + strlen(fname);

    char* part = get_next_path_part (cur, end);

    while (cur && fs) {
        /* keep looking on this level for a fs mounted on that dir */
        while (fs != NULL) {
            printf ("comparing '%s' and '%s'\n", fs->dirname, cur);
            if (strcmp (fs->dirname, cur) == 0) {
                printf ("\t found! breaking inner loop\n");
                break;
            }

            fs = fs->level_next;
        }

        found = fs;

        /* got one, load up its children if we have more path to match */
        if (fs->children) {
            fs = fs->children;
            printf ("trying to load children? %p\n", fs);
            cur = part;
            if (cur) {
                part = get_next_path_part (cur, end);
            }
            //break;
        } else {
            cur = part;
            break;
        }
    }

    if (found != NULL && cur && fs && fs != root) {
        *remaining = cur;
        *dest_fs = fs;

        return true;
    } else {
        return false;
    }
}

/*struct filesystem* dev = malloc (sizeof (struct filesystem));
    dev->dirname = "dev";
    dev->fs_ep_cap = 0;
    dev->buf_id = pawpaw_sbuf_get_id (buf);
    dev->badge = badge;
    dev->children = NULL;
    dev->level_next = NULL;

    dev->mounted_next = fs_head;
    fs_head = dev;

    root->children = dev;*/

int fs_register_info (struct pawpaw_event* evt) {
    for (int i = 0; i < 50; i++) {
        seL4_Yield();
    }

    if (!evt->share) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct filesystem* fs = malloc (sizeof (struct filesystem));
    fs->type = strdup ((char*)evt->share->buf);
    fs->cap = 0;
    fs->owner_badge = evt->badge;
    fs->next = fs_head;
    fs_head = fs;

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 0);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int fs_register_cap (struct pawpaw_event* evt) {
    struct filesystem* fs = fs_head;
    while (fs) {
        if (fs->owner_badge == evt->badge) {
            break;
        }

        fs = fs->next;
    }

    if (!fs) {
        printf ("vfs: failed to find matching fs\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    fs->cap = pawpaw_event_get_recv_cap ();
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 0);
    printf ("vfs: cool replying\n");
    return PAWPAW_EVENT_NEEDS_REPLY;
}

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   fs_register_info,   1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   fs_register_cap,    0,  HANDLER_REPLY                       },
    //{   fs_mount,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    //{   vfs_open,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers };

int main (void) {
    /* install root node */
    root = malloc (sizeof (struct fs_node));
    root->dirname = "";
    root->fs_ep_cap = 0;
    root->children = NULL;
    root->level_next = NULL;
    root->mounted_next = NULL;
    root->buf_id = 0;

    node_head = root;

    /* init event handler */
    pawpaw_event_init ();

    /* create our EP to listen on */
    seL4_CPtr service_cap = pawpaw_create_ep ();
    assert (service_cap);

    /* register and listen */
    pawpaw_register_service (service_cap);
    pawpaw_event_loop (&handler_table, service_cap);
    return 0;
#if 0
    while (1) {
            unsigned int slot = seL4_GetMR (2);

            sbuf_t buf;

            if (task != VFS_LINK_CAP) {
                if (seL4_MessageInfo_get_extraCaps (message) == 1) {
                    /* got a cap (should be a sbuf cap), so let's try to mount */
                    printf ("******* trying to mount ********\n");
                    buf = pawpaw_sbuf_mount (page_cap);

                    /* reset up for next slot */
                    page_cap = pawpaw_cspace_alloc_slot ();
                    assert (page_cap);

                    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, page_cap, PAPAYA_CSPACE_DEPTH);
                } else {
                    printf ("******* fetching %d ********\n", seL4_GetMR(1));
                    buf = pawpaw_sbuf_fetch (seL4_GetMR (1));
                }

                if (!buf) {
                    printf ("svc_vfs: invalid buffer\n");
                    continue;
                }
            }

            //if (seL4_GetMR (0) == )
            if (task == VFS_OPEN) {
                printf ("svc_vfs: VFS OPEN from %d\n", badge);

                /* get the requested filename */
                printf ("reading in str from %p (slot 0x%x)\n", pawpaw_sbuf_slot_get (buf, slot), slot);
                char* s = strdup (pawpaw_sbuf_slot_get (buf, slot));
                assert (s);

                printf ("passed in: %s\n", s);

                struct filesystem* fs;
                char* remaining;

                if (parse_filename (s, root, &fs, &remaining)) {
                    /* pass the buck to the FS layer to see if it knows anything about the file */
//                    #if 0
                    /* create the container + cap with a default size */
                    container_t container = pawpaw_mbox_container_allocate (16);
                    assert (container);

                    /* get a slot that we can send stuff with */
                    seL4_Word mbox_slot = pawpaw_mbox_get_sender_slot (container);

                    /* get the box associated with that slot */
                    void* mbox = pawpaw_mbox_get (box, mbox_slot);

                    /* copy in the data */
                    strcpy (mbox, cur);

                    /* prepare the message, with the cap to the container */
                    seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 1, 2);
                    seL4_SetCap (0, container->cap);
                    seL4_SetMR (0, VFS_OPEN);
                    seL4_SetMR (1, mbox_slot);

                    /* call it */
                    seL4_Call (fs->fs_ep_cap, lookup_msg);

                    /* and check if we found the file, caching if appropriate */
                    if (seL4_GetMR (0) == 0) {
                        printf ("AWESOME, FS REPORTS WE GOT THE FILE\n");
//#endif

                    printf ("+++++ LOOKING UP BUF ID %d\n", fs->buf_id);
                    sbuf_t fs_buf = pawpaw_sbuf_fetch (fs->buf_id);
                    if (!fs_buf) {
                        printf ("invalid buf id\n");
                        continue;
                    }

                    int fs_slot = pawpaw_sbuf_slot_next (fs_buf);
                    char* fs_filename = pawpaw_sbuf_slot_get (fs_buf, fs_slot);
                    assert (fs_filename);

                    /* copy it in AND GO - don't need to send cap since it was created when they did REGISTER */
                    strcpy (fs_filename, remaining);
                    printf ("copied %s to %p (slot %d)\n", fs_filename, fs_filename, fs_slot);
                    seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 0, 3);
                    
                    seL4_SetMR (0, VFS_OPEN);
                    seL4_SetMR (1, fs->buf_id);
                    seL4_SetMR (2, fs_slot);
                    seL4_Call (fs->fs_ep_cap, lookup_msg);

                } else {
                    printf ("file not found\n");
                    reply = seL4_MessageInfo_new (0, 0, 0, 1);
                    seL4_SetMR (0, -1);
                    seL4_Send (reply_cap, reply);
                }

                free (s);
         
        }
    }
#endif
}
