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

/* some trie thing */
struct fs_node {
    char* dirname;

    struct filesystem* fs;
    seL4_Word mounter_badge;    /* keep track of PID that mounted us */

    /* linked list of children */
    struct fs_node* children;
    struct fs_node* next;
};

struct filesystem {
    char* type;
    seL4_Word owner_badge;
    seL4_CPtr cap;
    struct pawpaw_share* share;
    struct filesystem* next;
};

struct fs_node* fs_root = NULL;
struct filesystem* fs_head = NULL; /* for iterating filesystems */

int parse_pathname (char* current_path_part, char* end, struct fs_node* fs,
    struct fs_node** dest_fs, char** remaining, int* best_depth, int level);

/*
 * find/replaces the next forward slash with an ASCII NUL.
 * param s now points to the current path part, and the
 * part that is returned is the NEXT path part, or NULL
 * if no more remains or you've gone past 'end'.
 *
 * mangles parameter s, so make sure you strdup if you need
 * the original.
 */
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

/* TODO: this could go away if you re-wrote it not to be recursive.. */
#define MAX_PATH_RECURSION  20

/*
 * assumes path is at least /, and initially passed in root
 * returns true if path fully consumed, false otherwise.
 */
int parse_pathname (char* current_path_part, char* end, struct fs_node* fs,
    struct fs_node** dest_fs, char** remaining, int* best_depth, int level) {

    if (level > MAX_PATH_RECURSION) {
        return false;
    }

    /* base case */
    if (current_path_part == NULL) {
        printf ("**** WOAH FOUND THE END AT %p LEVEL %d\n", fs, level);
        *remaining = NULL;
        *dest_fs = fs;
        *best_depth = level;
        return true;
    }

    /* changes current_path_part and next_path_part */
    /*if (level == 0) {
        current_path_part = get_next_path_part (current_path_part, end);
    }*/

    char* next_path_part = get_next_path_part (current_path_part, end);
    char* later_path_part = NULL;
    if (next_path_part) {
        later_path_part = get_next_path_part (next_path_part, end);
    }

    /* consume slashes */
    /*while (next_path_part && strlen (next_path_part) == 0) {
        current_path_part = next_path_part;
        next_path_part = get_next_path_part (current_path_part, end);
    }*/

    /* keep digging through all matching children filesystems
       recursively */
    struct fs_node* child = fs->children;
    while (child && next_path_part) {
        //printf ("-- child '%s' == '%s'?\n", child->dirname, next_path_part);
        if (strcmp (child->dirname, next_path_part) == 0) {
            /* sub-call NOT terminating here will set dest_fs and remaining */
            /* XXX: don't return, but keep checking if we want to allow nested
             * mounted FS ala FreeBSD */

            if (later_path_part) {
                /* put it back */
                *(char*)(later_path_part - 1) = '/';
            }

            if (parse_pathname (next_path_part, end, child, dest_fs, remaining,
                best_depth, level + 1)) {

                return true;
            }

            if (later_path_part) {
                *(char*)(later_path_part - 1) = '\0';
            }
        }

        child = child->next;
    }

    //printf ("ok but does '%s' == fs's '%s'?\n", current_path_part, fs->dirname);
    if (strcmp (current_path_part, fs->dirname) == 0) {
        //printf("!!YUP!!\n");
        /* ok wasn't a child this was as far as we got */
        if (level > *best_depth) {
            printf ("**** WOAH FOUND THE END AT %p LEVEL %d\n", fs, level);
            /* found deeper, update */
            *remaining = next_path_part;
            *dest_fs = fs;
            *best_depth = level;
        }

        /* if no more path remains, WE ARE IT! */
        if (next_path_part == NULL || *next_path_part == '\0') {
            return true;
        }
    }

    return false;
}

int fs_register_info (struct pawpaw_event* evt) {
    if (!evt->share) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct filesystem* fs = malloc (sizeof (struct filesystem));
    fs->type = strdup ((char*)evt->share->buf);
    fs->cap = 0;
    fs->owner_badge = evt->badge;
    fs->next = fs_head;
    fs->share = evt->share;
    fs_head = fs;

    evt->reply = seL4_MessageInfo_new (0, 0, 0, 0);
    evt->flags |= PAWPAW_EVENT_NO_UNMOUNT;
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
        return PAWPAW_EVENT_UNHANDLED;
    }

    assert (seL4_MessageInfo_get_extraCaps (evt->msg)  == 1);
    fs->cap = pawpaw_event_get_recv_cap ();
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 0);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int fs_mount (struct pawpaw_event* evt) {
    if (!evt->share) {
        printf ("vfs: no share\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* FIXME: should use some sort of copyin since can overflow + crash */
    char* mountpoint = strdup ((char*)evt->share->buf);
    char* end = mountpoint + strlen(mountpoint);
    char* orig_mountpoint = strdup (mountpoint);
    char* fstype = strdup ((char*)(evt->share->buf + strlen (mountpoint) + 1));

    if (mountpoint == end) {
        /* mountpoint empty */
        printf ("vfs: mountpoint empty\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* find the given filesystem type */
    struct filesystem* fs = fs_head;
    while (fs) {
        if (strcmp (fs->type, fstype) == 0) {
            break;
        }

        fs = fs->next;
    }

    if (!fs) {
        /* no such fs type */
        printf("vfs: unknown fs type '%s'\n", fstype);
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* FIXME: currently only handles mounts on root */
    char* remaining = NULL;
    struct fs_node* parent_fs = NULL;
    int best_depth = -1;

    /* allow mount on arbitrary directory - FIXME: does not check depth limit */
    int done = false;
    while (!done) {
        printf("parsing %s...\n", mountpoint);
        int consumed = parse_pathname (mountpoint, end, fs_root, &parent_fs,
            &remaining, &best_depth, 0);

        if (consumed) {
            printf("was fully consumed\n");
            if (parent_fs) {
                if (parent_fs->fs) {
                    /* already mounted on that path */
                    printf ("vfs: path already mounted\n");
                    return PAWPAW_EVENT_UNHANDLED;
                } else {
                    /* had structure but nothing mounted, seems OK */
                    done = true;
                    break;
                }
            }
        }

        printf("had path remaining\n");

        if (!remaining) {
            printf("or apparently not? setting back to %s\n", mountpoint);
            remaining = strdup(orig_mountpoint);
        }

        if (remaining) {
            printf("creating new node @ '%s'\n", remaining);

            /* create parent "folder" */
            struct fs_node *new_parent = malloc (sizeof (struct fs_node));
            new_parent->dirname = strdup(remaining);
            new_parent->fs = NULL;
            new_parent->mounter_badge = 0;
            new_parent->children = NULL;

            /* and attach it */
            new_parent->next = parent_fs->children;
            parent_fs->children = new_parent;

            /* reset depth count + path strings and go again */
            best_depth = -1;
            free (mountpoint);
            mountpoint = strdup (orig_mountpoint);
            end = mountpoint + strlen(mountpoint);
            printf("retrying\n");
        }
    }

    /* mount here */
    parent_fs->fs = fs;
    parent_fs->mounter_badge = evt->badge;   /* only mounter should unmount */

    /* done */
    free (fstype);
    free (orig_mountpoint);
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
    seL4_SetMR (0, 0);

    evt->flags |= PAWPAW_EVENT_NO_UNMOUNT;
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_open (struct pawpaw_event* evt) {
    if (!evt->share) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct fs_node* node = NULL;
    char* remaining = NULL;

    /* if the client changes this under us, weird stuff might happen so save
     * it now */
    char* requested_filename = strdup (evt->share->buf);
    char* orig_filename = strdup (requested_filename);
    int best_depth = -1;

    printf ("vfs: want to open '%s'\n", requested_filename);
    parse_pathname (requested_filename,
        requested_filename + strlen(requested_filename), fs_root, &node,
        &remaining, &best_depth, 0);

    if (best_depth > -1) {
        if (remaining) {
            printf ("had remaining path '%s'\n", remaining);
            int offset = remaining - requested_filename;
            strcpy (node->fs->share->buf, orig_filename + offset);
        } else {
            printf ("no more remaining path, zeroing\n");
            *(char*)node->fs->share->buf = '\0';
        }

        /* pass buck to the FS layer to see if it knows anything about file */
        seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 0, 4);
        
        seL4_SetMR (0, VFS_OPEN);
        seL4_SetMR (1, node->fs->share->id);
        seL4_SetMR (2, evt->args[0]);
        seL4_SetMR (3, evt->badge); /* owner */

        seL4_MessageInfo_t fs_reply = seL4_Call (node->fs->cap, lookup_msg);
        assert (seL4_MessageInfo_get_capsUnwrapped (fs_reply) == 0);
        if (seL4_MessageInfo_get_extraCaps (fs_reply) == 1) {
            /* SINCE I CAN'T FORWARD REPLY CAPS :* */
            evt->reply = seL4_MessageInfo_new (0, 0, 1, 1);
            seL4_SetCap (0, pawpaw_event_get_recv_cap());
            seL4_SetMR (0, 0);
        } else {
            printf ("vfs: open failed with err %d\n", seL4_GetMR (0));
            /* open failed - no cap came back */
            evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
            seL4_SetMR (0, -1);
        }
        //seL4_SetMR (0, status);
        /* don't need to set MR 0 since we use it from result of last call */
    } else {
        /* path failure - use orig_filename since other gets mangled */
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
    }

    free (requested_filename);
    free (orig_filename);

    //evt->flags |= PAWPAW_EVENT_NO_UNMOUNT;
    return PAWPAW_EVENT_NEEDS_REPLY;
}

/* XXX: merge logic with vfs_stat */
int vfs_listdir (struct pawpaw_event* evt) {
    if (!evt->share) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct fs_node* node = NULL;
    char* remaining = NULL;

    /* if the client changes this under us, weird stuff might happen; save it */
    ((char*)evt->share->buf)[PAPAYA_IPC_PAGE_SIZE - 1] = '\0';
    char* requested_filename = strdup (evt->share->buf);
    char* orig_filename = strdup (requested_filename);
    int best_depth = -1;

    printf ("** about to listdir on '%s' @ %d\n", requested_filename, evt->args[0]);

    parse_pathname (requested_filename,
        requested_filename + strlen(requested_filename), fs_root, &node,
        &remaining, &best_depth, 0);

    if (best_depth > -1) {
        /* path success */
        printf ("path success; best depth = %d\n", best_depth);
        if (remaining) {
            printf ("had remaining path '%s'\n", remaining);
            int offset = remaining - requested_filename;
            strcpy (node->fs->share->buf, orig_filename + offset);
        } else {
            printf ("no more remaining path, zeroing\n");
            *(char*)node->fs->share->buf = '\0';
        }

        /* pass to the FS layer to see if it knows anything about the file */
        seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 1, 4);
        
        seL4_SetMR (0, VFS_LISTDIR);
        seL4_SetMR (1, node->fs->share->id);
        seL4_SetMR (2, evt->args[0]);
        seL4_SetMR (3, evt->args[1]);
        seL4_SetCap (0, evt->share->cap);

        seL4_Call (node->fs->cap, lookup_msg);
        int success = seL4_GetMR (0);
        if (success == 0) {
            memcpy (evt->share->buf, node->fs->share->buf, sizeof (stat_t));
        }

        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, success);
    } else {
        /* path failure - use orig_filename since other gets mangled */
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
    }

    free (requested_filename);
    free (orig_filename);

    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_stat (struct pawpaw_event* evt) {
    if (!evt->share) {
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct fs_node* node = NULL;
    char* remaining = NULL;

    /* if the client changes this under us, weird stuff might happen; save it */
    ((char*)evt->share->buf)[PAPAYA_IPC_PAGE_SIZE - 1] = '\0';
    char* requested_filename = strdup (evt->share->buf);
    char* orig_filename = strdup (requested_filename);
    int best_depth = -1;

    printf ("** about to stat on '%s'\n", requested_filename);

    parse_pathname (requested_filename,
        requested_filename + strlen(requested_filename), fs_root, &node,
        &remaining, &best_depth, 0);

    if (best_depth > -1) {
        assert (node);
        /* path success */
        printf ("path success; best depth = %d\n", best_depth);
        if (remaining) {
            printf ("had remaining path '%s'\n", remaining);
            int offset = remaining - requested_filename;
            strcpy (node->fs->share->buf, orig_filename + offset);
        } else {
            printf ("no more remaining path, zeroing\n");
            *(char*)node->fs->share->buf = '\0';
        }

        /* pass to the FS layer to see if it knows anything about the file */
        seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 0, 2);
        
        seL4_SetMR (0, VFS_STAT);
        seL4_SetMR (1, node->fs->share->id);

        printf ("doing stat..\n");
        seL4_Call (node->fs->cap, lookup_msg);
        int success = seL4_GetMR (0);
        if (success == 0) {
            memcpy (evt->share->buf, node->fs->share->buf, sizeof (stat_t));
        }

        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, success);
    } else {
        /* path failure - use orig_filename since other gets mangled */
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
    }

    free (requested_filename);
    free (orig_filename);

    return PAWPAW_EVENT_NEEDS_REPLY;
}

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   fs_register_info,   1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   fs_register_cap,    0,  HANDLER_REPLY                       },
    {   fs_mount,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   vfs_open,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   0,  0,  0   },      //  read 
    {   0,  0,  0   },      //  write
    {   0,  0,  0   },      //  close
    {   vfs_listdir,        3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   vfs_stat,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers, "vfs" };

int main (void) {
    /* install root node */
    struct fs_node *root = malloc (sizeof (struct fs_node));
    assert (root);

    root->dirname = "";
    root->fs = NULL;
    root->mounter_badge = 0;
    root->children = NULL;
    root->next = NULL;

    fs_root = root;

    /* init event handler */
    pawpaw_event_init ();

    /* create our EP to listen on */
    seL4_CPtr service_cap = pawpaw_create_ep ();
    assert (service_cap);

    /* register and listen */
    pawpaw_register_service (service_cap);

    pawpaw_event_loop (&handler_table, NULL, service_cap);
    return 0;
}
