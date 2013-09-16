#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <pawpaw.h>

#include <sos.h>


/* some shit trie thing */
struct filesystem {
    char* dirname;

    seL4_CPtr fs_ep_cap;
    struct filesystem* children;

    struct filesystem* next;
};

struct filesystem* fs_head;

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

struct pawpaw_can* mycan;

int main(void) {
    printf ("svc_vfs: hello\n");
    int err;
    seL4_Word badge;
    seL4_MessageInfo_t message, reply;

    /* install root */
    struct filesystem* root = malloc (sizeof (struct filesystem));
    root->dirname = "";
    root->fs_ep_cap = 0;
    root->children = NULL;
    root->next = NULL;

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
        seL4_CPtr reply_cap = pawpaw_save_reply ();
        uint32_t label = seL4_MessageInfo_get_label(message);

        printf ("** SVC_VFS ** received message from %x with label %d and length %d\n", badge, label, seL4_MessageInfo_get_length (message));
        //printf ("   extraCaps = %d\n", seL4_MessageInfo_get_extraCaps (message));
        //printf ("   capsUnwrapped = %d\n", seL4_MessageInfo_get_capsUnwrapped (message));

        if (label == seL4_NoError) {
            if (seL4_GetMR (0) == SYSCALL_CAN_NEGOTIATE) {
                printf ("HELLO I AM DOG: allocating for %d\n", seL4_GetMR (2));
                struct pawpaw_can* can = pawpaw_can_allocate (seL4_GetMR (2));

                printf ("just created can %p at %d\n", can, seL4_GetMR(2));

                seL4_MessageInfo_t reply = seL4_MessageInfo_new (0, 0, 0, 2);
                seL4_SetMR (0, 16);
                seL4_SetMR (1, (seL4_Word)can);

                seL4_Send (reply_cap, reply);

            } else if (seL4_GetMR (0) == VFS_OPEN) {
                printf ("svc_vfs: VFS OPEN from %d\n", badge);
                struct pawpaw_can* can = pawpaw_can_fetch (badge);
                assert (can);

                char* s = pawpaw_bean_get (can, seL4_GetMR (1));
                printf ("passed in: %s\n", s);

                /* check cache for this filename
                 * if we find it, tell filesystem to "open file with inode" and the one they gave us when the said cache it
                 * if we find reverse cache, tell user straight away 
                 * otherwise, loop through all filesystems, asking them if they have this file

                 * FIXME: NEED TO DECIDE if cache works on fsid, then remaning path, or full path??

                 */

                /* walk mounted filesystems thing */

                struct filesystem* fs = fs_head;
                struct filesystem* found = NULL;
                char* cur = strdup (s);
                char* end = cur + strlen(s);

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

                        fs = fs->next;
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

                if (found != NULL && cur && fs) {
                    /* pass the buck to the FS layer to see if it knows anything about the file */
                    printf ("still had a filesystem, cool! just need to lookup: %s\n", cur);

                    seL4_MessageInfo_t lookup_msg = seL4_MessageInfo_new (0, 0, 0, 1);
                    struct pawpaw_can* can = pawpaw_can_fetch (badge);
                    if (!can) {
                        can = pawpaw_can_set (badge, pawpaw_can_negotiate (fs->fs_ep_cap, 16));
                    }

                    strcpy (pawpaw_bean_get (can, 0), cur);
                    seL4_SetMR (0, VFS_OPEN);
                    seL4_SetMR (1, 0);      // slot 0

                    seL4_Call (fs->fs_ep_cap, lookup_msg);
                } else {
                    printf ("file not found\n");
                    reply = seL4_MessageInfo_new (0, 0, 0, 1);
                    seL4_SetMR (0, VFS_FILE_NOT_FOUND);
                    seL4_Send (reply_cap, reply);
                }

                free (cur);
            } else if (seL4_GetMR (0) == VFS_MOUNT) {
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

            } else {
                printf ("svc_vfs: UNKNOWN MESSAGE %d\n", seL4_GetMR(0));
            }
        }
    }
}
