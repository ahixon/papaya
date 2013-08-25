/* address space management
 *  - so region based mapping 
 *  - stores a copy of the page table for this address space
 */

#include <sel4/sel4.h>

#include <cspace/cspace.h>

#include "ut_manager/ut.h"

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

	if (pd != 0) {
		printf ("addrspace_create: using provided pagedir cap 0x%x\n", pd);
		as->pagedir_cap = pd;
	} else {
		/* create a new page directory */
		printf ("addrspace_create: creating new pagedir\n");
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

	vaddr &= ~(PAGE_SIZE - 1);
	printf ("as_map_page: aligned vaddr = 0x%x\n", vaddr);

	struct as_region* reg = as_get_region_by_addr (as, vaddr);
	if (!reg) {
		printf ("as_map_page: vaddr 0x%x does not belong to any region\n", vaddr);
		return 0;
	}

	return page_map (as, reg, vaddr);
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
	struct as_region* last = as->regions;
     while (last != NULL && last->next != NULL) {
         last = last->next;
     }
 
     if (last != NULL) {
         last->next = reg;
     } else {
         as->regions = reg;
     }
}

struct as_region*
as_get_region_by_type (addrspace_t as, as_region_type type) {
	return as->special_regions[type];
}

struct as_region*
as_define_region (addrspace_t as, vaddr_t vbase, size_t size, seL4_CapRights permissions, as_region_type type) {
	printf ("as_define_region: before alignment: vbase = 0x%x, size = 0x%x\n", vbase, size);

	/* make sure we're page aligned */
	size += vbase & ~((vaddr_t)PAGE_MASK);
	vbase &= PAGE_MASK;

	size = (size + PAGE_SIZE - 1) & PAGE_MASK;

	printf ("as_define_region: after alignment:  vbase = 0x%x, size = 0x%x\n", vbase, size);

	if (vbase == 0) {
		printf ("as_create_region: mapping 0th page is invalid\n");
		return NULL;
	}

	struct as_region* reg = malloc (sizeof (struct as_region));
	if (!reg) {
		return NULL;
	}

	reg->vbase = vbase;
	reg->size = size;
	reg->permissions = permissions;
	//reg->type = type;

	if (as_region_overlaps (as, reg)) {
		free (reg);
		return NULL;
	}

	as_region_insert (as, reg);

	if (type != REGION_GENERIC) {
		as->special_regions[type] = reg;
	}

	return reg;
}

struct as_region*
as_resize_region (addrspace_t as, struct as_region* reg, size_t amount) {
	reg->size += amount;
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