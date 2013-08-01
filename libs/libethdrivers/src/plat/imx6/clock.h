#include <stdint.h>


enum clk_id {
    CLK_MASTER,
    CLK_PLL2,
    CLK_MMDC_CH0,
    CLK_AHB,
    CLK_IPG,
    CLK_ARM,
    CLK_ENET,
    CLK_CUSTOM
};

struct clock {
    enum clk_id id;
    const char* name;
    void *state;
    struct clock* (*init)(struct clock* clk);
    uint32_t (*get_freq)(struct clock*);
    uint32_t (*set_freq)(struct clock*, uint32_t hz);
    void (*recal)(struct clock*);
    uint32_t freq;
    /* For requesting a freq change */
    struct clock* parent;
    /* Provide in place linked list for parent */
    struct clock* sibling;
    /* For signalling a freq change down */
    struct clock* child;
};

struct clock* clk_init(struct clock*);
struct clock* clk_get_clock(enum clk_id id);
uint32_t clk_set_freq(struct clock* clk, uint32_t hz);
uint32_t clk_get_freq(struct clock* clk);
void clk_register_child(struct clock* parent, struct clock* child);
void clk_recal(struct clock* clk);


void clk_print_clock_tree(void);

