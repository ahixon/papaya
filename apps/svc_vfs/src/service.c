#include <assert.h>
#include <string.h>
#include <stdio.h>
#include <stdlib.h>
#include <inttypes.h>

#include <sel4/sel4.h>
#include <syscalls.h>

#include <cspace/cspace.h>

#include <sos.h>

#define RX_MAP      0xa0003000

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

int main(void) {

    int err;
    seL4_Word badge;
    seL4_MessageInfo_t message, reply;

    seL4_CPtr client_page_cap = SYSCALL_SERVICE_SLOT;

    seL4_CPtr pd_cap = 7;
    seL4_SetCapReceivePath (4, client_page_cap, CSPACE_DEPTH);

    /* install root */
    struct filesystem* root = malloc (sizeof (struct filesystem));
    root->dirname = "";
    root->fs_ep_cap = 0;
    root->children = NULL;
    root->next = NULL;

    fs_head = root;

    /* install /dev */
    struct filesystem* dev = malloc (sizeof (struct filesystem));
    dev->dirname = "dev";
    dev->fs_ep_cap = 0;
    dev->children = NULL;
    dev->next = NULL;

    root->children = dev;

    printf ("VFS service started + waiting...\n");

    while (1) {
        message = seL4_Wait((SYSCALL_SERVICE_SLOT + 1), &badge);
        uint32_t label = seL4_MessageInfo_get_label(message);

        printf ("** SVC_VFS ** received message from %x with label %d and length %d\n", badge, label, seL4_MessageInfo_get_length (message));
        printf ("   extraCaps = %d\n", seL4_MessageInfo_get_extraCaps (message));
        printf ("   capsUnwrapped = %d\n", seL4_MessageInfo_get_capsUnwrapped (message));

        if (label == seL4_NoError) {
            if (seL4_GetMR (0) == VFS_OPEN) {
                /* check to see if we got their cap */
                if (seL4_MessageInfo_get_extraCaps (message) != 1) {
                    printf ("vfs: asked to open but didn't send page cap, returning failure\n");

                    reply = seL4_MessageInfo_new (0, 0, 0, 1);
                    seL4_SetMR (0, VFS_INVALID_CAP);
                    seL4_Reply (reply);
                }

                /* cool ok have the cap, hopefully it's the right one (will find out when we map) */
                err = seL4_ARM_Page_Map (client_page_cap, pd_cap, RX_MAP, seL4_AllRights, seL4_ARM_Default_VMAttributes);
                if (err) {
                    printf ("vfs: failed to map page: %s\n", seL4_Error_Message (err));
                    break;
                }

                char* s = (char*)RX_MAP;
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
                    printf ("current part is: %s\n", cur);
                    printf ("next part is %s\n", part);
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
                    if (fs) {
                        fs = fs->children;
                        printf ("trying to load children? %p\n", fs);
                        cur = part;
                        part = get_next_path_part (cur, end);
                        //break;
                    }
                }

                if (found != NULL) {
                    /* pass the buck to the FS layer to see if it knows anything about the file */
                    printf ("still had a filesystem, cool!\n");
                } else {
                    printf ("file not found\n");
                    reply = seL4_MessageInfo_new (0, 0, 0, 1);
                    seL4_SetMR (0, VFS_FILE_NOT_FOUND);
                    seL4_Reply (reply);
                }

                free (cur);
            }
        }

    }
}
