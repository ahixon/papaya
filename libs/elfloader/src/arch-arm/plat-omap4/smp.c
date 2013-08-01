/* @LICENSE(NICTA_CORE) */

#include <stdint.h>
#include "../scu.h"

void flush_dcache();

void omap_write_auxcoreboot_addr(void *);
void omap_write_auxcoreboot0(uint32_t, uint32_t);
void omap_non_boot(void);

void init_cpus()
{
    scu_enable(0x48240000);
    omap_write_auxcoreboot_addr(omap_non_boot);
    flush_dcache();
    asm("dsb");
    asm("sev":::"memory");
    omap_write_auxcoreboot0(0x200, 0xfffffdff);
}
