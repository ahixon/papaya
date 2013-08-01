/* @LICENSE(NICTA_CORE) */

#include <autoconf.h>

#include "stdint.h"
#include "stdio.h"
#include "stdlib.h"
#include "string.h"

#include "elfloader.h"
#include "cpuid.h"

static struct image_info kernel_info;
static struct image_info user_info;

typedef void (*init_kernel_t)(paddr_t ui_p_reg_start,
                              paddr_t ui_p_reg_end, int32_t pv_offset, vaddr_t v_entry);

/* Assembly functions. */
extern int get_cpuid(void);
extern void flush_dcache(void);
extern void cpu_idle(void);

/* Platform functions */
void init_cpus(void);

/* Poor-man's lock. */
static volatile int non_boot_lock = 0;

#ifdef CONFIG_SMP_ARM_MPCORE
/* External symbols. */
extern int booting_cpu_id;
#endif

/* Entry point for all CPUs other than the initial. */
void non_boot_main(void)
{
#ifdef CONFIG_SMP_ARM_MPCORE
    /* Spin until the first CPU has finished intialisation. */
    while (!non_boot_lock) {
        cpu_idle();
    }

    /* Enable the MMU, and enter the kernel. */
    arm_enable_mmu();

    /* Jump to the kernel. */
    ((init_kernel_t)kernel_info.virt_entry)(user_info.phys_region_start,
                                            user_info.phys_region_end, user_info.phys_virt_offset,
                                            user_info.virt_entry);
#endif
}

/*
 * Entry point.
 *
 * Unpack images, setup the MMU, jump to the kernel.
 */
int main(void)
{
    int num_apps;
#ifdef CONFIG_SMP_ARM_MPCORE
    /* If not the boot strap processor then go to non boot main */
    if ( (read_cpuid_mpidr() & 0xf) != booting_cpu_id) {
        non_boot_main();
    }
#endif

    /* Print welcome message. */
    printf("\nELF-loader started.\n");
    printf("  paddr=[%p..%p]\n", _start, _end - 1);

    /* Unpack ELF images into memory. */
    load_images(&kernel_info, &user_info, 1, &num_apps);
    if (num_apps != 1) {
        printf("No user images loaded!\n");
        abort();
    }

    /* Setup MMU. */
    printf("Enabling MMU and paging\n");
    init_boot_pd(&kernel_info);
    arm_enable_mmu();

#ifdef CONFIG_SMP_ARM_MPCORE
    /* Bring up any other CPUs */
    init_cpus();
    non_boot_lock = 1;
#endif

    /* Enter kernel. */
    printf("Jumping to kernel-image entry point...\n\n");
    ((init_kernel_t)kernel_info.virt_entry)(user_info.phys_region_start,
                                            user_info.phys_region_end, user_info.phys_virt_offset,
                                            user_info.virt_entry);

    /* We should never get here. */
    printf("Kernel returned back to the elf-loader.\n");
    abort();
}
