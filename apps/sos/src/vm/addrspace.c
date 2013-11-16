/* address space management
 *  - so region based mapping 
 *  - stores a copy of the page table for this address space
 */

#include <string.h>
#include <sel4/sel4.h>

#include <cspace/cspace.h>

#include "../ut_manager/ut.h"

#include "vm.h"
#include "mapping.h"
#include "vmem_layout.h"
#include "frametable.h"
#include "pagetable.h"

#define verbose 5
#include <sys/debug.h>
#include <sys/panic.h>

#include <assert.h>

#define PAGE_SIZE (1 << seL4_PageBits)
#define PAGE_MASK (~(PAGE_SIZE - 1))

#define STACK_SIZE   (1 * 1024 * PAGE_SIZE)      /* 1 MB */

addrspace_t cur_addrspace = NULL;

/*static*/ void
addrspace_print_regions (addrspace_t as) {
    char* types[5] = {"STACK", "HEAP ", "IPC  ", "SHARE", "  -  "};

    struct as_region* reg = as->regions;
    printf ("\tvbase\t\tvlimit\t\tsize\t\tperms\tattrs\n");

    while (reg) {
        printf ("%s\t0x%08x\t0x%08x\t0x%08x\t%d\t%d\n", types[reg->type], reg->vbase, reg->vbase + reg->size, reg->size, reg->permissions, reg->attributes);
        reg = reg->next;
    }
}

addrspace_t
addrspace_create (seL4_ARM_PageTable pd)
{
    addrspace_t as = malloc (sizeof (struct addrspace));

    if (!as) {
        return NULL;
    }

    memset (as, 0, sizeof (struct addrspace));

    as->pagetable = pagetable_init ();
    if (!as->pagetable) {
        free (as);
        return NULL;
    }

    as->regions = NULL;

    if (pd != 0) {
        as->pagedir_cap = pd;
    } else {
        /* create a new page directory */
        int err;

        as->pagedir_addr = ut_alloc (seL4_PageDirBits);
        if (!as->pagedir_addr) {
            pagetable_free (as->pagetable);
            free (as);
            return NULL;
        }
        
        err = cspace_ut_retype_addr(as->pagedir_addr,
                                    seL4_ARM_PageDirectoryObject, seL4_PageDirBits,
                                    cur_cspace, &as->pagedir_cap);
        if (err) {
            ut_free (as->pagedir_addr, seL4_PageDirBits);
            pagetable_free (as->pagetable);
            free (as);
            return NULL;
        }
    }

    return as;
}

void
addrspace_destroy (addrspace_t as) {
    struct as_region* reg = as->regions;
    while (reg) {
        struct as_region* next = reg->next;
        /* don't need to use as_region_destroy since we don't need to reorder
         * as we're nuking everything anyway */
        free (reg);

        reg = next;
    }

    pagetable_free (as->pagetable);
    as->pagetable = NULL;

    if (as->pagedir_cap != seL4_CapInitThreadPD) {
        if (as->pagedir_cap) {
            cspace_delete_cap (cur_cspace, as->pagedir_cap);
        }

        if (as->pagedir_addr) {
            /* FIXME: seL4 bug? related to TCB binding bug */
            //ut_free (as->pagedir_addr, seL4_PageDirBits);
        }
    }

    free (as);
}