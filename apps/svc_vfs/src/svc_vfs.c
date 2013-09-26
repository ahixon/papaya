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
struct filesystem {
    char* dirname;

    seL4_CPtr fs_ep_cap;
    seL4_Word badge;
    seL4_Word buf_id;
    struct filesystem* children;

    struct filesystem* level_next;
    struct filesystem* mounted_next;
};

struct filesystem* fs_head; /* head for iterating */
struct filesystem* root;    /* ROOT NODE for walking trie */

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

int parse_filename (char* fname, struct filesystem* parent, struct filesystem** dest_fs, char** remaining) {
    /* check cache for this filename
     * if we find it, tell filesystem to "open file with inode" and the one they gave us when the said cache it
     * if we find reverse cache, tell user straight away 
     * otherwise, loop through all filesystems, asking them if they have this file

     * FIXME: NEED TO DECIDE if cache works on fsid, then remaining path, or full path??

     */

    /* walk mounted filesystems thing */

    struct filesystem* fs = parent;
    struct filesystem* found = NULL;
    char* cur = fname;
    char* end = cur + strlen(fname);

    char* part = get_next_path_part (cur, end);

    while (cur && fs) {
        //printf ("current part is: %s\n", cur);
        //printf ("next part is %s\n", part);
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

struct pawpaw_can* mycan;

int main(void) {
    printf ("svc_vfs: hello\n");
    //int err;
    seL4_Word badge;
    seL4_MessageInfo_t message, reply;

    /* install root */
    root = malloc (sizeof (struct filesystem));
    root->dirname = "";
    root->fs_ep_cap = 0;
    root->children = NULL;
    root->level_next = NULL;
    root->mounted_next = NULL;
    root->buf_id = 0;

    fs_head = root;

    /* create our EP to listen on */
    printf ("svc_vfs: creating EP\n");
    seL4_CPtr service_cap = pawpaw_create_ep ();
    assert (service_cap);

    printf ("svc_vfs: allocating slot\n");
    seL4_CPtr page_cap = pawpaw_cspace_alloc_slot ();
    assert (page_cap);

    seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, page_cap, PAPAYA_CSPACE_DEPTH);

    printf ("svc_vfs: registering service\n");
    pawpaw_register_service (service_cap);
    printf ("svc_vfs: ready\n");

    while (1) {
        /* wait for a message */
        message = seL4_Wait(service_cap, &badge);
        seL4_Word task = seL4_GetMR (0);
        seL4_CPtr reply_cap;

        if (task != VFS_LINK_CAP && task != VFS_REGISTER) {
            reply_cap = pawpaw_save_reply ();
        }

        uint32_t label = seL4_MessageInfo_get_label(message);

        if (label == seL4_NoError) {
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
                    #if 0
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
#endif

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
            } else if (task == VFS_LINK_CAP) {
                struct filesystem* fs = fs_head;
                while (fs) {
                    if (fs->badge == badge) {
                        fs->fs_ep_cap = page_cap;

                        /* ready for next */
                        page_cap = pawpaw_cspace_alloc_slot ();
                        assert (page_cap);

                        seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, page_cap, PAPAYA_CSPACE_DEPTH);
                        break;
                    }

                    fs = fs->level_next;
                }
            } else if (task == VFS_REGISTER) {
                printf ("done, getting slot %d\n", slot);
                char* name = pawpaw_sbuf_slot_get (buf, slot);
                printf ("registering filesystem called %s\n", name);

                /* FIXME: should register filesytem type
                 * in hindsight we should actually be spawning a new process */
                if (strcmp (name, "dev") == 0) {
                    /* FIXME: should actually mount this EXTERNALLY */
                    printf ("should mount\n");

                    struct filesystem* dev = malloc (sizeof (struct filesystem));
                    dev->dirname = "dev";
                    dev->fs_ep_cap = 0;
                    printf ("+++++ WAS USING BUF IDX %d\n", pawpaw_sbuf_get_id (buf));
                    dev->buf_id = pawpaw_sbuf_get_id (buf);
                    dev->badge = badge;
                    dev->children = NULL;
                    dev->level_next = NULL;

                    dev->mounted_next = fs_head;
                    fs_head = dev;

                    root->children = dev;
                }
            } else if (task == VFS_MOUNT) {
                #if 0
                char* path = pawpaw_bean_get (pawpaw_can_fetch (badge), seL4_GetMR (1));
                // MR2 = number of mount arguments concat'd in options str, WE IGNORE THIS FOR NOW

                if (seL4_MessageInfo_get_extraCaps (message) != 1) {
                    printf ("got no cap to mount with\n");
                    continue;
                }

                /* FIXME: take out the path parsing stuff and use it to install properly */
                printf ("mounting on %s\n", path);

                struct filesystem* dev = malloc (sizeof (struct filesystem));
                dev->dirname = "dev";
                dev->fs_ep_cap = page_cap;
                dev->children = NULL;
                dev->next = NULL;

                page_cap = pawpaw_cspace_alloc_slot ();
                assert (page_cap);

                seL4_SetCapReceivePath (PAPAYA_ROOT_CNODE_SLOT, page_cap, PAPAYA_CSPACE_DEPTH);

                fs_head->children = dev;

                reply = seL4_MessageInfo_new (0, 0, 0, 1);
                seL4_SetMR (0, 0);
                seL4_Send (reply_cap, reply);
#endif
                printf ("svc_vfs: wanted to mount\n");
            } else {
                printf ("svc_vfs: UNKNOWN MESSAGE %d from %d\n", seL4_GetMR(0), badge);
            }
        }
    }
}
