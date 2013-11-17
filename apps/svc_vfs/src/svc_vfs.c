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

int parse_pathname (char* current_path_part, char* end, struct fs_node* fs, struct fs_node** dest_fs, char** remaining, int* best_depth, int level);

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

/* this could go away if you re-wrote it not to be recursive.. */
#define MAX_PATH_RECURSION  20

/*
 * assumes path is at least /, and initially passed in root
 * returns true if path fully consumed, false otherwise.
 */
int parse_pathname (char* current_path_part, char* end, struct fs_node* fs, struct fs_node** dest_fs, char** remaining, int* best_depth, int level) {
    if (level > MAX_PATH_RECURSION) {
        return false;
    }

    for (int i = 0; i < level; i++) { printf ("\t"); }
    printf ("parse_pathname (%s, end, fs:%s, .., ..)\n", current_path_part ? current_path_part : "<NULL>", fs->dirname);

    /* base case */
    if (current_path_part == NULL) {
        *remaining = NULL;
        *dest_fs = fs;
        return true;
    }

    /* changes current_path_part and next_path_part */
    char* next_path_part = get_next_path_part (current_path_part, end);
    for (int i = 0; i < level; i++) { printf ("\t"); }
    printf ("next path part = %s\n", next_path_part ? next_path_part : "<NULL>");

    /* keep digging through all matching children filesystems
       recursively */
    struct fs_node* child = fs->children;
    while (child) {
        if (strcmp (child->dirname, current_path_part)) {
            /* sub-call NOT terminating here will set dest_fs and remaining */
            /* XXX: don't return, but keep checking if we want to allow nested mounted FS */
            if (parse_pathname (next_path_part, end, child, dest_fs, remaining, best_depth, level + 1)) {
                return true;
            }
        }

        child = child->next;
    }

    for (int i = 0; i < level; i++) { printf ("\t"); }
    printf ("comparing '%s' and '%s'\n", current_path_part, fs->dirname);
    if (strcmp (current_path_part, fs->dirname) == 0) {
        /* ok wasn't a child this was as far as we got */
        if (level > *best_depth) {
            printf ("* new best\n");
            /* found deeper, update */
            *remaining = next_path_part;
            *dest_fs = fs;
            *best_depth = level;
        }

        /* if no more path remains, WE ARE IT! */
        if (next_path_part == NULL || *next_path_part == '\0') {
            for (int i = 0; i < level; i++) { printf ("\t"); }
            printf ("success!\n");
            return true;
        }
    }

    for (int i = 0; i < level; i++) { printf ("\t"); }
    printf ("failed, next path part = %s\n", next_path_part ? next_path_part : "<NULL>");
    return false;
}

int fs_register_info (struct pawpaw_event* evt) {
    if (!evt->share) {
        printf ("fs_register_info: missing share\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct filesystem* fs = malloc (sizeof (struct filesystem));
    fs->type = strdup ((char*)evt->share->buf);
    fs->cap = 0;
    fs->owner_badge = evt->badge;
    fs->next = fs_head;
    fs->share = evt->share;
    fs_head = fs;

    printf ("vfs: registered info for '%s'\n", fs->type);

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
        printf ("vfs: failed to find matching fs\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    printf ("vfs: registered cap for %s\n", fs->type);

    assert (seL4_MessageInfo_get_extraCaps (evt->msg)  == 1);
    fs->cap = pawpaw_event_get_recv_cap ();
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 0);
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int fs_mount (struct pawpaw_event* evt) {
    if (!evt->share) {
        printf ("vfs_mount: missing share\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* FIXME: should use some sort of copyin since can buffer overflow + crash */
    char* mountpoint = strdup ((char*)evt->share->buf);
    char* end = mountpoint + strlen(mountpoint);
    char* orig_mountpoint = strdup (mountpoint);
    char* fstype = strdup ((char*)(evt->share->buf + strlen (mountpoint) + 1));

    if (mountpoint == end) {
        printf ("vfs: mountpoint was empty\n");
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
        printf("vfs_mount: could not find fs type '%s'\n", fstype);
        return PAWPAW_EVENT_UNHANDLED;
    }

    /* FIXME: currently only handles mounts on root */
    char* remaining = NULL;
    struct fs_node* parent_fs = NULL;
    int best_depth = -1;

    //get_next_path_part (mountpoint, end);

    /* allow mount on arbitrary directory - FIXME: does not check depth limit */
    int done = false;
    while (!done) {
        int consumed = parse_pathname (mountpoint, end, fs_root, &parent_fs, &remaining, &best_depth, 0);
        if (consumed) {
            if (parent_fs) {
                if (parent_fs->fs) {
                    /* already mounted on that path */
                    printf ("already mounted on %s\n", mountpoint);
                    return PAWPAW_EVENT_UNHANDLED;
                } else {
                    /* had structure but nothing mounted, seems OK */
                    break;
                }
            }
        }

        char* next_part = remaining || get_next_path_part (remaining, end);
        if (next_part) {
            /* create parent "folder" */
            printf ("creating parent fsnode %s\n", remaining);
            struct fs_node *new_parent = malloc (sizeof (struct fs_node));
            new_parent->dirname = remaining;
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
        }
    }

    /* mount here */
    parent_fs->fs = fs;
    parent_fs->mounter_badge = evt->badge;   /* only mounter should unmount(?) */

    /* done */
    printf ("vfs: mounted fs '%s' to %s\n", fstype, mountpoint);
    free (fstype);
    free (orig_mountpoint);
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 0);

    evt->flags |= PAWPAW_EVENT_NO_UNMOUNT;
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_open (struct pawpaw_event* evt) {
    if (!evt->share) {
        printf ("vfs_open: missing share\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct fs_node* node = NULL;
    char* remaining = NULL;

    /* if the client changes this under us, weird stuff might happen so save it now */
    char* requested_filename = strdup (evt->share->buf);
    char* orig_filename = strdup (requested_filename);
    int best_depth = -1;

    parse_pathname (requested_filename, requested_filename + strlen(requested_filename), fs_root, &node, &remaining, &best_depth, 0);

    if (remaining) {
        printf ("vfs: path success\n");
        int offset = remaining - requested_filename;
        strcpy (node->fs->share->buf, orig_filename + offset);

        /* pass the buck to the FS layer to see if it knows anything about the file */
        printf ("vfs: asking filesystem '%s' about '%s'\n", node->fs->type, node->fs->share->buf);
        seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 0, 4);
        
        seL4_SetMR (0, VFS_OPEN);
        seL4_SetMR (1, node->fs->share->id);
        seL4_SetMR (2, evt->args[0]);
        seL4_SetMR (3, evt->badge); /* owner */

        /* forward the reply */
        //assert (evt->reply_cap);
        //seL4_SetCap (0, evt->reply_cap);

        /* FIXME: need a callback ID - should be Send instead */
        //pawpaw_event_get_recv_cap();        /* XXX: investigate why we need this */
        

        seL4_MessageInfo_t fs_reply = seL4_Call (node->fs->cap, lookup_msg);
        //seL4_Word status = seL4_GetMR (0);
        assert (seL4_MessageInfo_get_capsUnwrapped (fs_reply) == 0);
        if (seL4_MessageInfo_get_extraCaps (fs_reply) == 1) {
            /* SINCE I CAN'T FORWARD CAPS THANKS SEL4?????????? */
            printf ("vfs: fs reported open success - had cap\n");
            evt->reply = seL4_MessageInfo_new (0, 0, 1, 1);
            seL4_SetCap (0, pawpaw_event_get_recv_cap());
            seL4_SetMR (0, 0);
        } else {
            printf ("vfs: fs reported open failed - no cap\n");
            evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
            seL4_SetMR (0, -1);
        }
        //seL4_SetMR (0, status);
        /* don't need to set MR 0 since we use it from result of last call */
    } else {
        /* path failure - use orig_filename since other gets mangled */
        printf ("vfs: path failure for '%s'\n", orig_filename);
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, -1);
    }

    free (requested_filename);
    free (orig_filename);

    //evt->flags |= PAWPAW_EVENT_NO_UNMOUNT;
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_stat (struct pawpaw_event* evt) {
    if (!evt->share) {
        printf ("vfs_stat: missing share\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct fs_node* node = NULL;
    char* remaining = NULL;

    /* if the client changes this under us, weird stuff might happen so save it now */
    ((char*)evt->share->buf)[0x1000 - 1] = '\0'; /* FIXME: yuck, const */
    char* requested_filename = strdup (evt->share->buf);
    char* orig_filename = strdup (requested_filename);
    int best_depth = -1;

    parse_pathname (requested_filename, requested_filename + strlen(requested_filename), fs_root, &node, &remaining, &best_depth, 0);

    if (remaining) {
        printf ("vfs: path success\n");
        int offset = remaining - requested_filename;
        strcpy (node->fs->share->buf, orig_filename + offset);

        /* pass the buck to the FS layer to see if it knows anything about the file */
        printf ("vfs: asking filesystem '%s' about '%s'\n", node->fs->type, node->fs->share->buf);
        seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 0, 2);
        
        seL4_SetMR (0, VFS_STAT);
        seL4_SetMR (1, node->fs->share->id);
        /*seL4_SetMR (2, evt->args[0]);
        seL4_SetMR (3, evt->args[1]);*/

        /* FIXME: need a callback ID - should be Send instead */
        //pawpaw_event_get_recv_cap();        /* XXX: investigate why we need this */
        

        seL4_MessageInfo_t fs_reply = seL4_Call (node->fs->cap, lookup_msg);
        int success = seL4_GetMR (0);
        if (success == 0) {
            memcpy (evt->share->buf, node->fs->share->buf, sizeof (stat_t));
        }
        evt->reply = seL4_MessageInfo_new (0, 0, 0, 1);
        seL4_SetMR (0, success);
        //seL4_SetMR (0, read);   /* shouldn't need this, really */
    } else {
        /* path failure - use orig_filename since other gets mangled */
        printf ("vfs: path failure for '%s'\n", orig_filename);
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
    {   0,  0,  0   },      //{   vfs_listdir,        3,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },      //              //
    {   vfs_stat,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },      //              //
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers, "vfs" };

int main (void) {
    /* install root node */
    printf ("svc_vfs: installing root node\n");
    struct fs_node *root = malloc (sizeof (struct fs_node));
    assert (root);
    
    root->dirname = "";
    root->fs = NULL;
    root->mounter_badge = 0;
    root->children = NULL;
    root->next = NULL;

    fs_root = root;

    /* init event handler */
    printf ("svc_vfs: calling pawpaw_event_init\n");
    pawpaw_event_init ();

    /* create our EP to listen on */
    printf ("svc_vfs: creating synch EP\n");
    seL4_CPtr service_cap = pawpaw_create_ep ();
    assert (service_cap);

    /* register and listen */
    printf ("svc_vfs: registering service\n");
    pawpaw_register_service (service_cap);

    printf ("svc_vfs: started; listening for events\n");
    pawpaw_event_loop (&handler_table, NULL, service_cap);
    return 0;
}