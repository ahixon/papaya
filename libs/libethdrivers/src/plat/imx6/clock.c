
#include "clock.h"
#include "../../os_iface.h"
#include "io.h"
#include "uboot/crm_regs.h"
#include <errno.h>
#include <assert.h>
#include <string.h>

#define CCM_PADDR 0x020C4000 
#define CCM_SIZE      0x4000

#define ALG_PADDR 0x020C8000
#define ALG_SIZE      0x1000

#define MHZ (1000*1000)

#define MIN(a,b) (((a) < (b))? (a) : (b))
#define MAX(a,b) (((a) > (b))? (a) : (b))
#define INRANGE(a, x, b) MIN(MAX(x, a), b)

#define FIN (24 * MHZ)

#define BIT(x)   (1UL << (x))


#define PLL_CLKGATE BIT(31)
#define PLL_STABLE  BIT(30)
#define PLL_FRAC(x) ((x) << 24)

volatile struct clock_regs {
    struct mxc_ccm_reg * ccm;
    struct mxc_alg_reg * alg;
} clk_regs = {.ccm = NULL, .alg = NULL};


struct clock ipg_clk;
struct clock ahb_clk;
struct clock mmdc_ch0_clk;
struct clock pll2_clk;
struct clock master_clk;
struct clock arm_clk;
struct clock enet_clk;


static void 
clk_print_tree(struct clock* clk, char* prefix){
    int depth = strlen(prefix);
    char new_prefix[depth + 2];
    strcpy(new_prefix, prefix);
    strcpy(new_prefix + depth, "|");
    while(clk != NULL){
        if(clk->sibling == NULL){
            strcpy(new_prefix + depth, " ");
        }
        printf("%s", new_prefix);
        printf("\\");
        printf("%s (%3d MHZ)\n", clk->name, clk_get_freq(clk)/MHZ);
        clk_print_tree(clk->child, new_prefix);
        clk = clk->sibling;
    }
}

void
clk_print_clock_tree(void){
    struct clock *clk = clk_get_clock(CLK_MASTER);
    clk_print_tree(clk, ""); 
}



/* Also known as PLL_SYS */
#define PLL2_PADDR 0x020C8030

struct pll2_regs {
#define PLL2_CTRL_LOCK          BIT(31)
#define PLL2_CTRL_PDFOFFSET_EN  BIT(18)
#define PLL2_CTRL_BYPASS        BIT(16)
#define PLL2_CTRL_BYPASS_SRC(x) ((x) << 14)
#define PLL2_CTRL_ENABLE        BIT(13)
#define PLL2_CTRL_PWR_DOWN      BIT(12)
#define PLL2_CTRL_DIVSEL        BIT(0)
    uint32_t ctrl;
    uint32_t ctrl_s;
    uint32_t ctrl_c;
    uint32_t ctrl_t;
#define PLL2_SS_STOP(x)         ((x) << 16)
#define PLL2_SS_EN              BIT(15)
#define PLL2_SS_STEP(x)         ((x) <<  0)
    uint32_t ss;
    uint32_t res0[3];
    uint32_t num;
    uint32_t res1[3];
    uint32_t denom;
    uint32_t res2[3];
};



#define CLK_OPS(clk) \
        .get_freq = _##clk##_get_freq, \
        .set_freq = _##clk##_set_freq, \
        .recal    = _##clk##_recal,    \
        .init     = _##clk##_init,     \
        .name     = #clk

/* generic */

static struct clock* clks[] = {
        [CLK_MASTER]   = &master_clk,
        [CLK_PLL2  ]   = &pll2_clk,
        [CLK_MMDC_CH0] = &mmdc_ch0_clk,
        [CLK_AHB]      = &ahb_clk,
        [CLK_IPG]      = &ipg_clk,
        [CLK_ARM]      = &arm_clk,
        [CLK_ENET]     = &enet_clk
    };

struct clock*
clk_get_clock(enum clk_id id){
    struct clock* clk = clks[id];
    return clk_init(clk);
}

struct clock*
clk_init(struct clock* clk){
    return clk->init(clk);
}

uint32_t 
clk_get_freq(struct clock* clk){
    return (clk->state)? clk->get_freq(clk) : clk->freq;
}

uint32_t 
clk_set_freq(struct clock* clk, uint32_t v){
    return (clk->state)? clk->set_freq(clk, v) : clk->freq;
}

void
clk_register_child(struct clock* parent, struct clock* child){
    child->parent = parent;
    child->sibling = parent->child;
    parent->child = child;
}

void 
clk_recal(struct clock* clk){
    clk->recal(clk);
}

/* MASTER_CLK */
uint32_t
_master_get_freq(struct clock* clk){
    return clk->freq;
}

uint32_t
_master_set_freq(struct clock* clk, uint32_t hz){
    /* Master clock frequency is fixed */
    (void)hz;
    return clk_get_freq(clk);
}

void
_master_recal(struct clock* clk){
    assert(0);
}

struct clock*
_master_init(struct clock* clk){
    if(clk->state == NULL){
        clk_regs.ccm = (struct mxc_ccm_reg*)RESOURCE(CCM);
        clk_regs.alg = (struct mxc_alg_reg*)RESOURCE(ALG);
        clk->state = (void*)&clk_regs;
    }
    return clk;
}

struct clock master_clk = {
        .id = CLK_MASTER,
        CLK_OPS(master),
        .freq = FIN,
        .state = NULL,
        .parent = NULL,
        .sibling = NULL,
        .child = &pll2_clk
    };


/* ARM_CLK */
uint32_t
_arm_get_freq(struct clock* clk){
    uint32_t div;
    uint32_t fout, fin;
    div = clk_regs.alg->pll_sys;
    div &= BM_ANADIG_PLL_SYS_DIV_SELECT;
    fin = clk_get_freq(clk->parent);
    fout = fin * div / 2;
    return fout;
}

uint32_t
_arm_set_freq(struct clock* clk, uint32_t hz){
    uint32_t div;
    uint32_t fin;
    uint32_t v;

    fin = clk_get_freq(clk->parent);
    div = 2 * hz / fin;
    div = INRANGE(54, div, 108);
    /* bypass on during clock manipulation */
    clk_regs.alg->pll_sys_set = BM_ANADIG_PLL_SYS_BYPASS;
    /* Set the divisor */
    v = clk_regs.alg->pll_sys & ~(BM_ANADIG_PLL_SYS_DIV_SELECT);
    v |= div;
    clk_regs.alg->pll_sys = v;
    /* wait for lock */
    while(!(clk_regs.alg->pll_sys & BM_ANADIG_PLL_SYS_LOCK));
    /* bypass off */
    clk_regs.alg->pll_sys_clr = BM_ANADIG_PLL_SYS_BYPASS;
    CLK_DEBUG(printf("Set CPU frequency to %d Mhz.", clk_get_freq(clk)/MHZ));
    return clk_get_freq(clk);
}


void
_arm_recal(struct clock* clk){
    assert(0);
}

struct clock*
_arm_init(struct clock* clk){
    if(clk->state == NULL){
        clk_init(clk->parent);
        clk_register_child(clk->parent, clk);
        clk->state = (void*)&clk_regs;
    }
    return clk;
}

struct clock arm_clk = {
        .id = CLK_ARM,
        CLK_OPS(arm),
        .freq = 792 * MHZ,
        .state = NULL,
        .parent = &master_clk,
        .sibling = NULL,
        .child = NULL
    };


/* ENET_CLK */

static uint32_t
_enet_get_freq(struct clock* clk){
    uint32_t div;
    uint32_t fin;
    fin = clk_get_freq(clk->parent);
    div = clk_regs.alg->pll_enet;
    div &= BM_ANADIG_PLL_ENET_DIV_SELECT;
    switch(div){
        case 3:  return 5 * fin;
        case 2:  return 4 * fin;
        case 1:  return 2 * fin;
        case 0:  return 1 * fin;
        default: return 0 * fin;
    }
}


static uint32_t
_enet_set_freq(struct clock* clk, uint32_t hz){
    uint32_t div, fin;
    uint32_t v;
    fin = clk_get_freq(clk->parent);
    if(hz >= 5 * fin){
        div = 3;
    }else if(hz >= 4 * fin){
        div = 2;
    }else if(hz >= 2 * fin){
        div = 1;
    }else if(hz >= 1 * fin){
        div = 0;
    }else{
        div = 0;
    }
    /* bypass on */
    clk_regs.alg->pll_enet_set = BM_ANADIG_PLL_ENET_BYPASS;
    v = BM_ANADIG_PLL_ENET_ENABLE | BM_ANADIG_PLL_ENET_BYPASS;
    clk_regs.alg->pll_enet = v;
    /* Change the frequency */
    v = clk_regs.alg->pll_enet & ~(BM_ANADIG_PLL_ENET_DIV_SELECT);
    v |= div;
    clk_regs.alg->pll_enet = v;
    while(!(clk_regs.alg->pll_enet & BM_ANADIG_PLL_ENET_LOCK));
    /* bypass off */
    clk_regs.alg->pll_enet_clr = BM_ANADIG_PLL_ENET_BYPASS;
    CLK_DEBUG(printf("Set ENET frequency to %d Mhz... ", clk_get_freq(clk)/MHZ));
    return clk_get_freq(clk);
}

static void
_enet_recal(struct clock* clk){
    assert(0);
}

static struct clock* 
_enet_init(struct clock* clk){
    if(clk->state == NULL){
        clk_init(clk->parent);
        clk_register_child(clk->parent, clk);
        clk->state = (void*)&clk_regs;
    }
    return clk;
}

struct clock enet_clk = {
        .id = CLK_ENET,
        CLK_OPS(enet),
        .freq = 48 * MHZ,
        .state = NULL,
        .parent = &master_clk,
        .sibling = NULL,
        .child = NULL 
    };



/* PLL2_CLK */
uint32_t
_pll2_get_freq(struct clock* clk){
    uint32_t p, s;
    struct pll2_regs *regs = (struct pll2_regs*)
            ( (uint32_t)clk_regs.alg + (PLL2_PADDR & 0xfff) );
   
    assert((regs->ctrl & PLL2_CTRL_LOCK) != 0);
    assert((regs->ctrl & PLL2_CTRL_BYPASS) == 0);
    assert((regs->ctrl & PLL2_CTRL_PWR_DOWN) == 0);
    /* pdf offset? */

    p = clk_get_freq(clk->parent);
    if(regs->ctrl & PLL2_CTRL_DIVSEL){
        s = 22;
    }else{
        s = 20;
    }
    return p * s;
}

uint32_t
_pll2_set_freq(struct clock* clk, uint32_t hz){
    uint32_t s = hz/clk_get_freq(clk->parent);
    (void)s; /* TODO implemenet */
    assert(hz == 528 * MHZ);
    return clk_get_freq(clk);
}

void
_pll2_recal(struct clock* clk){
    assert(0);
}

struct clock*
_pll2_init(struct clock* clk){
    assert(clk->parent == &master_clk);
    if(clk->state == NULL){
        clk_init(clk->parent);
        clk_register_child(clk->parent, clk);
        clk->state = (void*)&clk_regs;
    }
    return clk;
}

struct clock pll2_clk = {
        .id = CLK_PLL2,
        CLK_OPS(pll2),
        .freq = 528 * MHZ,
        .state = NULL,
        .parent = &master_clk,
        .sibling = NULL,
        .child = &mmdc_ch0_clk,
    };

/* MMDC_CH0_CLK */
uint32_t
_mmdc_ch0_get_freq(struct clock* clk){
    return clk_get_freq(clk->parent);
}

uint32_t
_mmdc_ch0_set_freq(struct clock* clk, uint32_t hz){
    /* TODO there is a mux here */
    assert(hz == 528*MHZ);
    return clk_set_freq(clk->parent, hz);
}

void
_mmdc_ch0_recal(struct clock* clk){
    assert(0);
}

struct clock*
_mmdc_ch0_init(struct clock* clk){
    assert(clk->parent == &pll2_clk);
    if(clk->state == NULL){
        clk_init(clk->parent);
        clk_register_child(clk->parent, clk);
        clk->state = (void*)&clk_regs;
    }
    return clk;
}

struct clock mmdc_ch0_clk = {
        .id = CLK_MMDC_CH0,
        CLK_OPS(mmdc_ch0),
        .freq = 528 * MHZ,
        .state = NULL,
        .parent = &pll2_clk,
        .sibling = NULL,
        .child = NULL 
    };

/* AHB_CLK_ROOT */
uint32_t
_ahb_get_freq(struct clock* clk){
    return clk_get_freq(clk->parent)/4;
}

uint32_t
_ahb_set_freq(struct clock* clk, uint32_t hz){
    return clk_set_freq(clk->parent, hz*4);
}

void
_ahb_recal(struct clock* clk){
    assert(0);
}

struct clock*
_ahb_init(struct clock* clk){
    assert(clk->parent == &mmdc_ch0_clk);
    if(clk->state == NULL){
        clk_init(clk->parent);
        clk_register_child(clk->parent, clk);
        clk->state = (void*)&clk_regs;
    }
    return clk;
}

struct clock ahb_clk = {
        .id = CLK_AHB,
        CLK_OPS(ahb),
        .freq = 132 * MHZ,
        .state = NULL,
        .parent = &mmdc_ch0_clk,
        .sibling = NULL,
        .child = NULL,
    };

/* IPG_CLK_ROOT */
uint32_t 
_ipg_get_freq(struct clock* clk){
    return clk_get_freq(clk->parent) / 2;
};

uint32_t 
_ipg_set_freq(struct clock* clk, uint32_t hz){
    return clk_set_freq(clk->parent, hz * 2);
};

void
_ipg_recal(struct clock* clk){
    assert(0);
}

struct clock*
_ipg_init(struct clock* clk){
    assert(clk->parent == &ahb_clk);
    if(clk->state == NULL){
        clk_init(clk->parent);
        clk_register_child(clk->parent, clk);
        clk->state = (void*)&clk_regs;
    }
    return clk;
}

struct clock ipg_clk = {
        .id = CLK_IPG,
        CLK_OPS(ipg),
        .freq = 66000000UL,
        .state = NULL,
        .parent = &ahb_clk,
        .sibling = NULL,
        .child = NULL
    };

