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

#define PAGE_SIZE (1 << seL4_PageBits)

struct as_region REGION_TEST = {seL4_ARM_Default_VMAttributes, seL4_AllRights, 0, 0};

struct as_region*
as_get_region (addrspace_t as, vaddr_t vaddr) {
	return &REGION_TEST;
}

addrspace_t
addrspace_create (seL4_ARM_PageTable pd)
{
	printf ("mallocing struct\n");
	addrspace_t as = malloc (sizeof (struct addrspace));

	if (!as) {
		return NULL;
	}

	printf ("initialising pagetable\n");
	as->pagetable = pagetable_init();
	if (!as->pagetable) {
		free (as);
		return NULL;
	}

	as->regions = NULL;

	printf ("setting pagedir cap\n");
	if (pd != 0) {
		as->pagedir_cap = pd;
	} else {
		printf ("creating new pagedir\n");
		/* create a new page directory */
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

int
as_map_page (addrspace_t as, vaddr_t vaddr) {
	/* check if vaddr in a region */

	vaddr &= ~(PAGE_SIZE - 1);
	printf ("as_map_page: aligned vaddr = 0x%x\n", vaddr);

	struct as_region* reg = as_get_region (as, vaddr);
	if (!reg) {
		printf ("as_map_page: vaddr 0x%x does not belong to region in address space\n", vaddr);
		return false;
	}

	return page_map (as, reg, vaddr);
}