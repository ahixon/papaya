/* @LICENSE(GPLv2) */
/* Unsure of Licenscing, code for SCU was determined by carefully looking at
   Linux kernel source. see arch/arm/march-imx/ *.c, will assume GLPv2 as a result */

/* Platform specific definitions required for smp. Code contains few comments as it is black
   magic copied from Linux and I could not begin to comment it */

#include <autoconf.h>
#include <stdint.h>
#include "../scu.h"
#include "../stdio.h"

#ifdef CONFIG_SMP_ARM_MPCORE

/* System Reset Controller base address */
#define SRC_BASE 0x020D8000

#define SRC_SCR             0x000
#define SRC_GPR1            0x020
#define BP_SRC_SCR_WARM_RESET_ENABLE    0
#define BP_SRC_SCR_CORE1_RST        14
#define BP_SRC_SCR_CORE1_ENABLE     22

#define REG(base,offset) (*(volatile uint32_t*)(((uint32_t)(base))+(offset)))

void imx_non_boot(void);

static void *get_scu_base(void)
{
    void *scu;
    asm("mrc p15, 4, %0, c15, c0, 0" : "=r" (scu));
    return scu;
}

static void src_init(void)
{
    uint32_t val;
    val = REG(SRC_BASE, SRC_SCR);
    val &= ~(1 << BP_SRC_SCR_WARM_RESET_ENABLE);
    REG(SRC_BASE, SRC_SCR) = val;
}

static void src_enable_cpu(int cpu)
{
    uint32_t mask, val;

    mask = 1 << (BP_SRC_SCR_CORE1_ENABLE + cpu - 1);
    val = REG(SRC_BASE, SRC_SCR);
    val |= mask;
    REG(SRC_BASE, SRC_SCR) = val;
}

static void src_set_cpu_jump(int cpu, void *jump_addr)
{
    REG(SRC_BASE, SRC_GPR1 + cpu * 8) = (uint32_t)jump_addr;
}


void init_cpus(void)
{
    unsigned int i, num;
    void *scu = get_scu_base();

    scu_enable(scu);
    src_init();

    num = scu_get_core_count(scu);
#ifdef CONFIG_MAX_NUM_NODES
    if (num > CONFIG_MAX_NUM_NODES) {
        num = CONFIG_MAX_NUM_NODES;
    }
#endif
    printf("Bringing up %d other cpus\n", num - 1);
    for (i = 1; i < num; i++) {
        src_set_cpu_jump(i, imx_non_boot);
        src_enable_cpu(i);
    }
}

#endif
