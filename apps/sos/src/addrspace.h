#include "pagetable.h"

#ifndef __ADDRSPACE_H__
#define __ADDRSPACE_H__

struct addrspace {
	seL4_ARM_PageDirectory pagedir_cap;
	seL4_Word pagedir_addr;

	struct region* regions;
	pagetable_t pagetable;
};

typedef struct addrspace * addrspace_t;

struct as_region {
	seL4_ARM_VMAttributes attributes;
	seL4_CapRights permissions;

	vaddr_t from;
	vaddr_t to;
};

addrspace_t
addrspace_create (seL4_ARM_PageTable pd);

int
as_map_page (addrspace_t as, vaddr_t vaddr);

#endif