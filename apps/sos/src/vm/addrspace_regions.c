#include <sel4/sel4.h>

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

/* Region handling */

int
as_region_overlaps (addrspace_t as, struct as_region* region_check) {
     struct as_region* region = as->regions;
     while (region) {
        /* don't check ourselves */
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
        if (vaddr >= region->vbase && vaddr < (region->vbase + region->size)) {
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
as_define_region (addrspace_t as, vaddr_t vbase, size_t size,
    seL4_CapRights permissions, as_region_type type) {

    /* make sure we're page aligned */
    size += vbase & ~((vaddr_t)PAGE_MASK);
    vbase &= PAGE_MASK;

    size = (size + PAGE_SIZE - 1) & PAGE_MASK;

    if (vbase == 0) {
        return NULL;
    }

    if (size == 0) {
        return NULL;
    }

    struct as_region* reg = malloc (sizeof (struct as_region));
    if (!reg) {
        return NULL;
    }

    reg->vbase = vbase;
    reg->size = size;
    reg->permissions = permissions;
    reg->type = type;
    reg->attributes = DEFAULT_ATTRIBUTES;
    reg->next = NULL;
    reg->linked = NULL;
    reg->owner = as;

    if (as_region_overlaps (as, reg)) {
        free (reg);
        return NULL;
    }

    as_region_insert (as, reg);

    assert (type >= 0 && type <= REGION_GENERIC);
    if (type != REGION_GENERIC) {
        /* special region (ie heap) - store it for O(1) lookup */
        as->special_regions[type] = reg;
    }

    return reg;
}

void
as_region_destroy (addrspace_t as, struct as_region* kill) {
    struct as_region* region = as->regions;
    struct as_region* prev = NULL;

    /* reorder list */
    while (region != NULL) {
        if (region == kill) {
            break;
        }

        prev = region;
        region = region->next;
    }

    if (prev) {
        prev->next = kill->next;
    } else {
        as->regions = kill->next;
    }

    /* and free it */
    free (kill);
}

struct as_region*
as_get_region_after (addrspace_t as, vaddr_t vaddr) {
    struct as_region* region = as->regions;

    /* find a region that contains the fault address */
    while (region != NULL) {
        if (region->vbase >= vaddr) {
            break;
        }

        region = region->next;
    }

    return region;
}

struct as_region*
as_define_region_within_range (addrspace_t as, vaddr_t low, vaddr_t high,
    size_t size, seL4_CapRights permissions, as_region_type type) {

    /* check for gap at start */
    struct as_region* first = as_get_region_after (as, low);
    if (!first || first->vbase - size >= low) {
        /* nobody after, we are first */
        return as_define_region (as, low, size, permissions, type);
    }

    /* ok check after first before high */
    struct as_region* current = first;
    while (current) {
        struct as_region* next = current->next;
        if (next) {
            if (current->vbase + current->size + size < current->vbase) {
                /* went off the cliff (overflow) */
                return NULL;
            }

            if (current->vbase + current->size + size <= next->vbase) {
                /* internal fragmentation if using variable sizes in a range */
                return as_define_region (as, current->vbase + current->size,
                    size, permissions, type);
            } 
        } else {
            /* nothing after */
            return as_define_region (as, current->vbase + current->size, size,
                permissions, type);
        }

        current = next;
    }

    /* if we got here we ran out */
    return NULL;
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
    /* TODO: page align size? */
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

/* FIXME: this function should go.. */
seL4_CPtr inline
as_get_page_cap (addrspace_t as, vaddr_t vaddr) {
    assert (as != NULL);

    struct pt_entry* page = page_fetch_existing (as->pagetable, vaddr);
    if (!page) {
        return 0;
    }

    return page->cap;
}

struct as_region*
as_create_region_largest (addrspace_t as, seL4_CapRights permissions,
    as_region_type type) {

    vaddr_t cur_addr = PAGE_SIZE;   /* don't start on 0th page */

    vaddr_t largest_vaddr = cur_addr;
    size_t largest_extent = 0;

    /* TODO: probably should go to top of virtual memory rather than only
     * within existing regions. Works OK at the moment for processes since we
     * define IPC buffer at (near) the top. */
    struct as_region* reg = as->regions;
    while (reg) {
        size_t size = reg->vbase - cur_addr;

        if (size > largest_extent) {
            largest_extent = size;
            largest_vaddr = cur_addr;
        }

        /* start looking from address is end of current region */
        cur_addr = reg->vbase + reg->size;
        reg = reg->next;
    }

    if (largest_extent == 0) {
        return NULL;
    }

    return as_define_region (as, largest_vaddr, largest_extent, permissions,
        type);
}

/* returns the upper half of the divided region - lower rounds up */
struct as_region*
as_divide_region (addrspace_t as, struct as_region* reg,
    as_region_type upper_type) {

    vaddr_t region_top = reg->vbase + reg->size;

    /* ensure evenly divisble by 2 */
    if (reg->size % 2 != 0) {
        return NULL;
    }

    reg->size = reg->size / 2;
    reg->size = (reg->size + PAGE_SIZE - 1) & PAGE_MASK;

    // FIXME: probably should sanity check results (ie upper > lower vaddrs)
    vaddr_t upper_vaddr = reg->vbase + reg->size;

    struct as_region* upper_reg = as_define_region (as, upper_vaddr,
        region_top - upper_vaddr, reg->permissions, upper_type);

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

    if (as_region_overlaps (as, reg)) {
        reg->vbase -= amount;
        reg->size += amount;
        return false;
    }

    return true;
}

int
as_create_stack_heap (addrspace_t as, struct as_region** stack,
    struct as_region** heap) {

    struct as_region* cur_stack = as_create_region_largest (as, seL4_AllRights,
        REGION_STACK);

    if (!cur_stack) {
        return false;
    }

    /* create a guard page and move stack for one page of heap to start with */
    vaddr_t heap_vbase = cur_stack->vbase;

    if (!as_region_shift (as, cur_stack, PAGE_SIZE + PAGE_SIZE)) {
        return false;
    }

    /* record the current stack page so if go below this we update our ptr */
    as->stack_vaddr = cur_stack->vbase + cur_stack->size;

    struct as_region* cur_heap = as_define_region (as, heap_vbase, PAGE_SIZE,
        seL4_AllRights, REGION_HEAP);

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

vaddr_t
as_resize_heap (addrspace_t as, size_t amount) {
    amount = (amount + PAGE_SIZE - 1) & PAGE_MASK;

    struct as_region* heap = as_get_region_by_type (as, REGION_HEAP);

    if (amount == 0) {
        return heap->vbase;
    }

    struct as_region* stack = as_get_region_by_type (as, REGION_STACK);

    /* would wrap around memory? */
    vaddr_t new_vaddr = heap->vbase + heap->size + amount;
    if (new_vaddr < (heap->vbase + heap->size)) {
        return 0;
    }

    /* ensure that we're not trying to move it over our guard page or last
     * thing we hit in the stack */
    if (new_vaddr >= as->stack_vaddr) {
        return 0;
    }

    vaddr_t old_heap_vaddr = heap->vbase + heap->size;

    /* seems OK, try to move it and check we don't collide with anything else */
    if (heap && stack) {
        if (!as_region_shift (as, stack, amount)) {
            return 0;
        }

        heap = as_resize_region (as, heap, amount);
    }

    return old_heap_vaddr;
}