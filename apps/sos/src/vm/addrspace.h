#include "pagetable.h"

#ifndef __ADDRSPACE_H__
#define __ADDRSPACE_H__

#include <vm/vm.h>

#define NUM_SPECIAL_REGION_TYPES	4
typedef enum {
	REGION_STACK,
	REGION_HEAP,
	REGION_IPC,
	REGION_BEANS,
	REGION_GENERIC,
} as_region_type;

typedef enum {
	REGION_FROM_TOP,
	REGION_FROM_BOTTOM
} as_region_direction;

struct addrspace {
	seL4_ARM_PageDirectory pagedir_cap;
	seL4_Word pagedir_addr;

	struct as_region* regions;
	struct as_region* special_regions[NUM_SPECIAL_REGION_TYPES];
	pagetable_t pagetable;

	vaddr_t stack_vaddr;
};

struct as_region {
	seL4_ARM_VMAttributes attributes;
	seL4_CapRights permissions;

	vaddr_t vbase;
	size_t size;
	as_region_type type;

	struct as_region* next;

	/* for shared regions */
	addrspace_t owner;
	struct as_region* linked;
};

extern addrspace_t cur_addrspace;

addrspace_t
addrspace_create (seL4_ARM_PageTable pd);

void
addrspace_destroy (addrspace_t as);

frameidx_t
as_map_page (addrspace_t as, vaddr_t vaddr);

struct as_region*
as_resize_region (addrspace_t as, struct as_region* reg, size_t amount);

struct as_region*
as_define_region (addrspace_t as, vaddr_t vbase, size_t size, seL4_CapRights permissions, as_region_type type);

struct as_region*
as_get_region_by_type (addrspace_t as, as_region_type type);

struct as_region*
as_get_region_by_addr (addrspace_t as, vaddr_t start);

seL4_CPtr
as_get_page_cap (addrspace_t as, vaddr_t vaddr);

struct as_region*
as_create_region_largest (addrspace_t as, seL4_CapRights permissions, as_region_type type);

/* returns the upper half of the divided region */
struct as_region*
as_divide_region (addrspace_t as, struct as_region* reg, as_region_type upper_type);

int
as_create_stack_heap (addrspace_t as, struct as_region** stack, struct as_region** heap);

vaddr_t
as_resize_heap (addrspace_t as, size_t amount);

void
addrspace_print_regions (addrspace_t as);

#endif