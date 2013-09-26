/* address space management
 *  - so region based mapping 
 *  - stores a copy of the page table for this address space
 */

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

/*static*/ void
addrspace_print_regions (addrspace_t as) {
    char* types[5] = {"STACK", "HEAP ", "IPC  ", "BEANS", "  -  "};

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

    as->pagetable = pagetable_init();
    if (!as->pagetable) {
        free (as);
        return NULL;
    }

    as->regions = NULL;
    as->stack_vaddr = 0;

    if (pd != 0) {
        printf ("addrspace_create: using provided pagedir cap 0x%x\n", pd);
        as->pagedir_cap = pd;
    } else {
        /* create a new page directory */
        //printf ("addrspace_create: creating new pagedir\n");
        int err;

        as->pagedir_addr = ut_alloc(seL4_PageDirBits);
        if (!as->pagedir_addr) {
            pagetable_free (as->pagetable);
            free (as);
            return NULL;
        }
        
        err = cspace_ut_retype_addr(as->pagedir_addr,
                                    seL4_ARM_PageDirectoryObject,
                                    seL4_PageDirBits,
                                    cur_cspace,
                                    &as->pagedir_cap);
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
    pagetable_free (as->pagetable);
    as->pagetable = NULL;

    /* FIXME: free regions list */
    /* FIXME: free page directory - doesn't matter if root since we never destroy it! */
}

frameidx_t
as_map_page (addrspace_t as, vaddr_t vaddr) {
    /* check if vaddr in a region */
    assert (as != NULL);

    vaddr &= ~(PAGE_SIZE - 1);
    //printf ("as_map_page: aligned vaddr = 0x%x\n", vaddr);

    struct as_region* reg = as_get_region_by_addr (as, vaddr);
    if (!reg) {
        printf ("as_map_page: vaddr 0x%x does not belong to any region\n", vaddr);
        return 0;
    }

    /* stack grows downwards */
    if (as->special_regions[REGION_STACK] == reg) {
        //printf ("wanted to map page on stack (stack addr = 0x%x, vaddr = 0x%x\n", as->stack_vaddr, vaddr);
        if (vaddr < as->stack_vaddr) {
            //printf ("was less, updating stack vaddr = 0x%x\n", vaddr);
            as->stack_vaddr = vaddr;
        }
    }

    if (reg->linked) {
        //printf ("mapping linked pages\n");
        /* page in shared should be same offset from base (if linked, REGIONS ARE SAME SIZE) */
        vaddr_t offset = vaddr - reg->vbase;

        /*printf ("src pagetable\n");
        pagetable_dump (as->pagetable);

        printf ("src regions\n");
        addrspace_print_regions (as);

        printf ("dst pagetable\n");
        pagetable_dump (reg->linked->owner->pagetable);

        printf ("dst regions\n");
        addrspace_print_regions (reg->linked->owner);

        printf ("mapping...\n");*/
        struct pt_entry* entry = page_map_shared (as, reg, vaddr, reg->linked->owner, reg->linked, reg->linked->vbase + offset, false);
        /*printf ("\tmapped shared 0x%x and 0x%x (offset was %d) to underlying frame %p\n", vaddr, reg->linked->vbase + offset, offset, entry);

        printf ("src pagetable\n");
        pagetable_dump (as->pagetable);

        printf ("dst pagetable\n");
        pagetable_dump (reg->linked->owner->pagetable);*/

        return entry->frame_idx;
    } else {
        return page_map (as, reg, vaddr);
    }
}

/* Region handling */

int
as_region_overlaps (addrspace_t as, struct as_region* region_check) {
     struct as_region* region = as->regions;
     while (region) {
        /* don't check ourselves - FIXME: is this correct? */
        if (region != region_check) {
            vaddr_t check_base = region_check->vbase;
            vaddr_t check_limit = check_base + region_check->size;

            vaddr_t reg_base = region->vbase;
            vaddr_t reg_limit = reg_base + region->size;

            if (check_base < reg_limit && check_limit > reg_base) {
                return true;
            }
        }

        region = region->next;
     }
 
     return false;
 }

struct as_region*
as_get_region_by_addr (addrspace_t as, vaddr_t vaddr) {
    struct as_region* region = as->regions;

    /* find a region that contains the fault address */
    while (region != NULL) {
        if (vaddr >= region->vbase && vaddr <= (region->vbase + region->size)) {
            break;
        }

        region = region->next;
    }

    return region;
}

void 
as_region_insert (addrspace_t as, struct as_region* reg) {
    struct as_region *prev, *cur;
    cur = as->regions;
    prev = NULL;

    if (!cur) {
        as->regions = reg;
    } else {
        while (cur) {
            if (cur->vbase > reg->vbase) {
                break;
            }

            prev = cur;
            cur = cur->next;
        }

        reg->next = cur;

        if (prev) {
            prev->next = reg;
        } else {
            as->regions = reg;
        }
    }
}

struct as_region*
as_get_region_by_type (addrspace_t as, as_region_type type) {
    return as->special_regions[type];
}

struct as_region*
as_define_region (addrspace_t as, vaddr_t vbase, size_t size, seL4_CapRights permissions, as_region_type type) {
    //printf ("as_define_region: before alignment: vbase = 0x%x, size = 0x%x\n", vbase, size);

    /* make sure we're page aligned */
    size += vbase & ~((vaddr_t)PAGE_MASK);
    vbase &= PAGE_MASK;

    size = (size + PAGE_SIZE - 1) & PAGE_MASK;

    //printf ("as_define_region: after alignment:  vbase = 0x%x, size = 0x%x\n", vbase, size);

    if (vbase == 0) {
        printf ("as_create_region: mapping 0th page is invalid\n");
        return NULL;
    }

    struct as_region* reg = malloc (sizeof (struct as_region));
    if (!reg) {
        printf ("as_create_region: no more space to malloc\n");
        return NULL;
    }

    reg->vbase = vbase;
    reg->size = size;
    reg->permissions = permissions;
    reg->type = type;
    reg->attributes = seL4_ARM_Default_VMAttributes;
    reg->next = NULL;
    reg->linked = NULL;
    reg->owner = as;

    if (as_region_overlaps (as, reg)) {
        printf ("as_create_region: requested region 0x%x -> 0x%x overlaps:\n", vbase, vbase + size);
        addrspace_print_regions(as);
        free (reg);
        return NULL;
    }

    //printf ("inserting region 0x%x -> 0x%x\n", vbase, vbase + size);
    as_region_insert (as, reg);

    assert (type >= 0 && type <= REGION_GENERIC);
    if (type != REGION_GENERIC) {
        //printf ("as_create_region: inserting into special regions\n");
        as->special_regions[type] = reg;
    }

    return reg;
}

int
as_region_link (struct as_region* us, struct as_region* them) {
    if (us->linked || them->linked) {
        return false;
    }

    us->linked = them;
    them->linked = us;
    return true;
}

struct as_region*
as_resize_region (addrspace_t as, struct as_region* reg, size_t amount) {
    /* FIXME: should be page aligned! */
    size_t old_size = reg->size;
    reg->size += amount;

    /* check for wrap around */
    if (reg->size < old_size) {
        reg->size = old_size;
        return NULL;
    }

    if (as_region_overlaps (as, reg)) {
        reg->size -= amount;
        return NULL;
    }

    return reg;
}

seL4_CPtr
as_get_page_cap (addrspace_t as, vaddr_t vaddr) {
    assert (as != NULL);

    struct pt_entry* page = page_fetch (as->pagetable, vaddr);
    if (!page) {
        return 0;
    }

    return frametable_fetch_cap (page->frame_idx);
}

struct as_region*
as_create_region_largest (addrspace_t as, seL4_CapRights permissions, as_region_type type) {
    vaddr_t cur_addr = PAGE_SIZE;   /* don't start on 0th page */

    vaddr_t largest_vaddr = cur_addr;
    size_t largest_extent = 0;

    /* FIXME: probably should go to top of virtual memory rather than only within existing regions */
    /* Works OK at the moment for processes since we define IPC buffer at (near) the top */
    struct as_region* reg = as->regions;
    while (reg) {
        size_t size = reg->vbase - cur_addr;
        //printf ("extent of free space from 0x%x -> 0x%x = 0x%x\n", cur_addr, reg->vbase, size);

        if (size > largest_extent) {
            //printf ("beats our previous free size of 0x%x\n", largest_extent);
            largest_extent = size;
            largest_vaddr = cur_addr;
        }

        /* start looking from address is end of current region */
        cur_addr = reg->vbase + reg->size;
        //printf ("this region goes from 0x%x -> 0x%x\n", reg->vbase, cur_addr);
        reg = reg->next;
    }

    if (largest_extent == 0) {
        return NULL;
    }

    //printf ("ok using 0x%x size 0x%x\n", largest_vaddr, largest_extent);

    return as_define_region (as, largest_vaddr, largest_extent, permissions, type);
}

/* returns the upper half of the divided region - lower rounds up */
struct as_region*
as_divide_region (addrspace_t as, struct as_region* reg, as_region_type upper_type) {
    /* FIXME: make sure reg is in addrspace?? probably want to check all funcs too */
    /* FIXME: make sure reg->size is EVENLY divideable by 2 */

    vaddr_t region_top = reg->vbase + reg->size;

    reg->size = reg->size / 2;
    reg->size = (reg->size + PAGE_SIZE - 1) & PAGE_MASK;
    /*size += vbase & ~((vaddr_t)PAGE_MASK);
    vbase &= PAGE_MASK;*/

    vaddr_t upper_vaddr = reg->vbase + reg->size;

    printf ("as_divide_region: upper region vstart = 0x%x, len = 0x%x\n", upper_vaddr, region_top - upper_vaddr);
    printf ("                  lower region vstart = 0x%x, len = 0x%x\n", reg->vbase, reg->size);

    // FIXME: probably also want to sanity check results (ie upper > lower vaddrs)

    struct as_region* upper_reg = as_define_region (as, upper_vaddr, region_top - upper_vaddr, reg->permissions, upper_type);
    return upper_reg;
}

int
as_region_shift (addrspace_t as, struct as_region* reg, int amount) {
    vaddr_t old_vbase = reg->vbase;

    reg->vbase += amount;
    reg->size -= amount;

    /* check for wrap around */
    if (reg->vbase < old_vbase) {
        reg->vbase = old_vbase;
        reg->size += amount;

        return false;
    }

    /* FIXME: do we need this */
    if (as_region_overlaps (as, reg)) {
        reg->vbase -= amount;
        reg->size += amount;
        return false;
    }

    return true;
}

int
as_create_stack_heap (addrspace_t as, struct as_region** stack, struct as_region** heap) {
    struct as_region* cur_stack = as_create_region_largest (as, seL4_AllRights, REGION_STACK);
    //conditional_panic (!stack, "could not create large stack region\n");
    if (!cur_stack) {
        return false;
    }

    /* create a guard page and move stack for one page of heap to start with */
    vaddr_t heap_vbase = cur_stack->vbase;
    //printf ("heap vbase = 0x%x\n", heap_vbase);

    if (!as_region_shift (as, cur_stack, PAGE_SIZE + PAGE_SIZE)) {
        panic ("uhh failed to move stack up?");
    }

    //printf ("stack vbase (from heap vbase) now = 0x%x\n", as->special_regions[REGION_STACK]->vbase);

    /* record the current stack page so that if go below this we update our pointer */
    as->stack_vaddr = cur_stack->vbase + cur_stack->size;
    //printf ("setting stack vaddr to 0x%x\n", as->stack_vaddr);

    struct as_region* cur_heap = as_define_region (as, heap_vbase, PAGE_SIZE, seL4_AllRights, REGION_HEAP);

    if (cur_stack->size < STACK_SIZE) {
        return false;
    }

    /* and set a fixed stack size: FIXME: remove me if you don't want this! */
    cur_stack->size = STACK_SIZE;
    cur_stack->vbase = as->stack_vaddr - STACK_SIZE;

    if (cur_stack && heap) {
        if (stack != NULL) {
            *stack = cur_stack;
        }

        if (heap != NULL) {
            *heap = cur_heap;
        }

        return true;
    } else {
        return false;
    }
}

/*struct as_region**/
vaddr_t
as_resize_heap (addrspace_t as, size_t amount) {
    amount = (amount + PAGE_SIZE - 1) & PAGE_MASK;
    //printf ("asked for 0x%x, rounded to 0x%x\n", old_amount, amount);

    struct as_region* heap = as_get_region_by_type (as, REGION_HEAP);

    if (amount == 0) {
        return heap->vbase;
    }

    struct as_region* stack = as_get_region_by_type (as, REGION_STACK);
    //printf ("currently, stack vaddr = 0x%x\n", as->stack_vaddr);
    //printf ("old heap vaddr = 0x%x\n", heap->vbase);

    /* would wrap around memory? */
    vaddr_t new_vaddr = heap->vbase + heap->size + amount;
    if (new_vaddr < (heap->vbase + heap->size)) {
        return 0;
    }

    //printf ("new (tenative) heap vaddr = 0x%x\n", new_vaddr);

    /* ensure that we're not trying to move it over our guard page or last thing we hit in the stack */
    if (new_vaddr >= as->stack_vaddr) {
        //printf ("went past boundary 0x%x\n", as->stack_vaddr);
        return 0;
    }

    vaddr_t old_heap_vaddr = heap->vbase + heap->size;

    /* seems OK, try to move it and check we don't collide with anything else */
    if (heap && stack) {
        //printf ("OK cool, shifting stack up by 0x%x\n", amount);
        if (!as_region_shift (as, stack, amount)) {
            printf ("that failed\n");
            return 0;
        }

        //as->stack_vaddr += (amount + PAGE_SIZE);

        //printf ("stack now 0x%x -> 0x%x\n", stack->vbase, stack->vbase + stack->size);

        //printf ("ok, finally resizing heap by 0x%x\n", amount);
        heap = as_resize_region (as, heap, amount);

        if (heap) {
            //printf ("heap now 0x%x -> 0x%x\n", heap->vbase, heap->vbase + heap->size);
        } else {
            printf ("well that failed\n");
        }
    }

    //printf ("==========\n");
    //addrspace_print_regions(as);

    return old_heap_vaddr;
}