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
    seL4_Word mounter_badge;
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
    struct pawpaw_share* share;
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
    struct fs_node* fs = parent;
    struct fs_node* found = NULL;
    char* cur = fname;
    char* end = cur + strlen(fname);

    char* part = get_next_path_part (cur, end);

    while (cur && fs) {
        /* keep looking on this level for a fs mounted on that dir */
        while (fs != NULL) {
            //printf ("comparing '%s' and '%s'\n", fs->dirname, cur);
            if (strcmp (fs->dirname, cur) == 0) {
                //printf ("matched\n");
                break;
            }

            //printf ("no match, next\n");
            fs = fs->level_next;
            /* FIXME: holy shit this code needs a lot more thought */
            //fs = fs->mounted_next;
        }

        found = fs;

        /* got one, load up its children if we have more path to match */
        cur = part;
        //printf ("remaining = %p, children = %p\n", cur, fs->children);
        if (fs && fs->children) {
            fs = fs->children;
            //printf ("trying to load children? %p\n", fs);
            if (cur) {
                part = get_next_path_part (cur, end);
            }
        } else {
            break;
        }
    }

    if (found != NULL && cur && fs && fs != root) {
        *remaining = cur;
        *dest_fs = fs;
        return true;
    }

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
    char* fstype = strdup ((char*)(evt->share->buf + strlen (mountpoint) + 1));

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
    struct fs_node* node = malloc (sizeof (struct filesystem));
    node->dirname = mountpoint;
    node->fs = fs;
    node->mounter_badge = evt->badge;   /* only mounter should unmount(?) */
    node->children = NULL;
    node->level_next = root->children;

    node->mounted_next = node_head;
    node_head = node;

    root->children = node;
    //root->children->level_next = node;

    /* done */
    printf ("vfs: mounted fs '%s' to /%s\n", fstype, mountpoint);
    free (fstype);
    evt->reply = seL4_MessageInfo_new (0, 0, 0, 0);

    evt->flags |= PAWPAW_EVENT_NO_UNMOUNT;
    return PAWPAW_EVENT_NEEDS_REPLY;
}

int vfs_open (struct pawpaw_event* evt) {
    if (!evt->share) {
        printf ("vfs_open: missing share\n");
        return PAWPAW_EVENT_UNHANDLED;
    }

    struct fs_node* node;
    char* remaining;

    /* if the client changes this under us, weird stuff might happen so save it now */
    char* requested_filename = strdup (evt->share->buf);
    char* orig_filename = strdup (requested_filename);

    if (parse_filename (requested_filename, root, &node, &remaining)) {
        // printf ("vfs: path success\n");
        strcpy (node->fs->share->buf, remaining);

        /* pass the buck to the FS layer to see if it knows anything about the file */
        printf ("vfs: asking filesystem '%s' about '%s'\n", node->fs->type, remaining);
        seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 0, 4);
        
        seL4_SetMR (0, VFS_OPEN);
        seL4_SetMR (1, node->fs->share->id);
        seL4_SetMR (2, evt->args[1]);
        seL4_SetMR (3, evt->badge); /* owner */

        /* forward the reply */
        //assert (evt->reply_cap);
        //seL4_SetCap (0, evt->reply_cap);

        /* FIXME: need a callback ID - should be Send instead */
        pawpaw_event_get_recv_cap();        /* XXX: investigate why we need this */
        

        seL4_MessageInfo_t fs_reply = seL4_Call (node->fs->cap, lookup_msg);
        assert (seL4_MessageInfo_get_capsUnwrapped (fs_reply) == 0);
        assert (seL4_MessageInfo_get_extraCaps (fs_reply) >= 1);

        /* SINCE I CAN'T FORWARD CAPS THANKS SEL4?????????? */
        evt->reply = seL4_MessageInfo_new (0, 0, 1, 1);
        /* gonna forward MR 0 */
        seL4_SetCap (0, pawpaw_event_get_recv_cap());
        seL4_SetMR (0, 0);
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

struct pawpaw_eventhandler_info handlers[VFS_NUM_EVENTS] = {
    {   fs_register_info,   1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   fs_register_cap,    0,  HANDLER_REPLY                       },
    {   fs_mount,           1,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   vfs_open,           2,  HANDLER_REPLY | HANDLER_AUTOMOUNT   },
    {   0,  0,  0   },      //              //
};

struct pawpaw_event_table handler_table = { VFS_NUM_EVENTS, handlers, "vfs" };

int main (void) {
    /* install root node */
    root = malloc (sizeof (struct fs_node));
    root->dirname = "";
    root->fs = NULL;
    root->mounter_badge = 0;
    root->children = NULL;
    root->level_next = NULL;
    root->mounted_next = NULL;

    node_head = root;

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