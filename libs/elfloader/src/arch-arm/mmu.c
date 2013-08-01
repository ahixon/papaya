/* @LICENSE(NICTA_CORE) */

#include "stdint.h"
#include "elfloader.h"

#define ARM_SECTION_BITS 20

/*
 * Create a "boot" page directory, which contains a 1:1 mapping below
 * the kernel's first vaddr, and a virtual-to-physical mapping above the
 * kernel's first vaddr.
 */
void init_boot_pd(struct image_info *kernel_info)
{
    uint32_t i;
    vaddr_t first_vaddr = kernel_info->virt_region_start;
    paddr_t first_paddr = kernel_info->phys_region_start;

    /* identity mapping below kernel window */
    for (i = 0; i < (first_vaddr >> ARM_SECTION_BITS); i++) {
        _boot_pd[i] = (i << ARM_SECTION_BITS)
                      | BIT(10) /* kernel-only access */
#ifdef ARMV5
                      | BIT(4)  /* must be set for ARMv5 */
#endif
                      | BIT(1); /* 1M section */
    }

    /* mapping of kernel window */
    for (i = 0; i < ((-first_vaddr) >> ARM_SECTION_BITS); i++) {
        _boot_pd[i + (first_vaddr >> ARM_SECTION_BITS)]
            = ((i << ARM_SECTION_BITS) + first_paddr)
              | BIT(10) /* kernel-only access */
#ifdef ARMV5
              | BIT(4)  /* must be set for ARMv5 */
#endif
              | BIT(1); /* 1M section */
    }
}


