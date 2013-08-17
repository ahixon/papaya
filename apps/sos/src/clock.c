#include <assert.h>
#include <stddef.h>
#include <string.h>

#include <cspace/cspace.h>
#include <sys/panic.h>

#include <clock/clock.h>
#include <sys/debug.h>

#include "mapping.h"
#include "clock_gpt.h"

#define verbose 0
#define TICK_100MS (100 * 1000)

/* A array-based heap would be probably better here, but alas, I am lazy */
struct timer_node {
    uint64_t compare;       // what we set into the compare reg
    uint32_t overflows;     // number of overflows before compare reg is valid
    seL4_CPtr owner;        // who set the timer

    struct timer_node* next;
};

volatile struct gpt_regs* regs = NULL;

struct timer_node* timers = NULL;
uint32_t overflows = 0;

seL4_CPtr handler;

static void fire_timer (void);

/*
 * Clears all the GPT registers.
 */
static void
gpt_reset (void) {
    regs->control = 0;
    regs->interrupt = 0;
    regs->status = 0;
}

/*
 * Selects a clock source for the GPT. Saves the current interrupt register.
 * Does not re-enable the GPT after the clock source has been changed.
 *
 * See section 30.4.1 of the i.MX reference manual for more information.
 * Note that software reset and clock source steps should be swapped, since
 * the documentation is incorrect (SWR *will*, in fact, reset the CLKSRC bit).
 *
 */ 
static void
gpt_select_clock (int src)
{
    uint32_t prev_interrupts = regs->interrupt;

    regs->control &= ~CR_EN;
    regs->interrupt = 0;
    regs->control &= ~(CR_OM3(OUTPUT_ACTIVELOW) | CR_OM2(OUTPUT_ACTIVELOW) | CR_OM1(OUTPUT_ACTIVELOW));
    regs->control &= ~(CR_IM1(IM_BOTH) | CR_IM2(IM_BOTH));

    regs->control |= CR_SWR;
    regs->control &= ~CR_CLKSRC (CLOCK_CRYSTAL);
    regs->control |= CR_CLKSRC (src);

    regs->status = 0;
    regs->control |= CR_ENMOD;
    regs->interrupt = prev_interrupts;
}

/*
 * Sets the GPT prescaler.
 *
 * Valid scale values are between 1 and 4096.
 */
static void
gpt_set_prescale (uint32_t scale) {
    regs->prescale = scale & 0xFFF;
}

/*
 * Inserts a timer into the (sorted) active timer list.
 */
static void
insert_timer (struct timer_node* t) {
    if (timers == NULL) {
        timers = t;
    } else {
        struct timer_node* cur = timers;
        struct timer_node* prev = NULL;
        bool inserted = false;

        while (cur != NULL) {
            if (t->overflows > cur->overflows || t->compare > cur->compare) {
                prev = cur;
                cur = cur->next;
            } else {
                if (prev) {
                    prev->next = t;
                } else {
                    timers = t;
                }

                t->next = cur;
                inserted = true;
                break;
            }
        }

        if (!inserted) {
            prev->next = t;
        }
    }
}

/*
 * Creates a new timer. You will probably want to add it to the
 * timer list using insert_timer().
 */
static struct timer_node*
create_timer (uint64_t delay, seL4_CPtr owner)
{
    uint32_t counter = regs->counter;
    struct timer_node* timer = malloc (sizeof (struct timer_node));
    assert (timer != NULL);

    timer->overflows = delay / 0xffffffff;

    uint64_t initdelay = delay % 0xffffffff;
    timer->compare = initdelay + counter;

    if (timer->compare < initdelay) {
        timer->overflows++;
    }

    timer->owner = owner;
    timer->next = NULL;

    return timer;
}

/*
 * Removes and frees a given timer in the active timer list (if found).
 */
static void
destroy_timer (struct timer_node* target) {
    struct timer_node* t = timers;
    struct timer_node* prev = NULL;
    while (t) {
        if (t->compare == target->compare && t->overflows == target->overflows) {
            // update list
            if (prev) {
                prev->next = t->next;
            } else {
                timers = t->next;
            }

            // and free the timer
            free (t);
            break;
        }

        t = t->next;
    }
}

/*
 * Installs the first active timer into the GPT's compare
 * registers.
 */
static void
install_timers (void) {
    struct timer_node* t = timers;

    if (t && t->overflows == 0) {
        regs->interrupt |= IR_OC1;
        uint32_t now = regs->counter;
        regs->ocr[0] = t->compare;

        /* we missed this timer, so fire it and re-load */
        // FIXME: > or >= ???
        if (now > t->compare) {
            fire_timer ();
            install_timers();
        }
    } else {
        regs->interrupt &= ~IR_OC1;
    }
}


/*
 * Fires a timer with a given expiry.
 * 
 * Wakes up the client who originally registered it, and then
 * destroys the timer.
 */
static void
fire_timer (void) {
    //struct timer_node* t = find_timer (expiry);
    struct timer_node* t = timers;

    assert (t != NULL);
    assert (t->overflows == 0);

    printf ("** TIMER FIRED\tscheduled wakeup=%lld, owner=%p\n", t->compare, (void*)t->owner);
    // FIXME: actually wake up the client when we have IPC

    destroy_timer (t);
}

/* Public API */

/*
 * Handles a timer interrupt.
 */
void
handle_timer(void)
{
    // clear status flags
    uint32_t status = regs->status;
    regs->status = 0xfff;

    // and ack the interrupt
    int err;
    err = seL4_IRQHandler_Ack(handler);
    conditional_panic(err, "Failure to acknowledge pending interrupts");

    // now actually process it using our saved status
    if (regs->status & SR_ROV) {
        //printf ("rollover occured\n");
        struct timer_node* t = timers;
        while (t) {
            if (t->overflows > 0) {
                t->overflows--;
            } else {
                // this may happen if we were "late" and towards the top end of 32-bit counter value
                // but counter rolls over while we were checking if we were late, and hence we look "on-time"
                fire_timer();
            }

            t = t->next;
        }

        regs->status &= ~SR_ROV;
    }

    if (status & SR_OF1) {
        fire_timer ();
        install_timers();
    }

    // check OC3 for weird 100ms tick
    if (status & SR_OF3) {
        printf ("** 100ms tick, timestamp = %lld\n", time_stamp());
        regs->ocr[2] = regs->ocr[2] + TICK_100MS;
    }
}

int
start_timer(seL4_CPtr interrupt_ep)
{
    int err;

    if (regs && regs->control & CR_EN) {
        err = stop_timer();
        if (err != CLOCK_R_OK) {
            return err;
        }
    }

    if (!handler) {
        handler = cspace_irq_control_get_cap(cur_cspace, seL4_CapIRQControl, IRQ_GPT);

        err = seL4_IRQHandler_SetEndpoint(handler, interrupt_ep);
        conditional_panic(err, "Failed to set interrupt endpoint");

        err = seL4_IRQHandler_Ack(handler);
        conditional_panic(err, "Failure to acknowledge pending interrupts");

        regs = map_device ((void*)GPT_MEMMAP_BASE, GPT_MEMMAP_SIZE);
        conditional_panic(!regs, "Could not map GPT device");
    }

    gpt_reset();
    gpt_select_clock (CLOCK_PERIPHERAL);

    regs->control |= CR_OM1 (OUTPUT_SET) | CR_OM2(OUTPUT_SET) | CR_OM3(OUTPUT_SET) | CR_FRR;
    regs->interrupt |= IR_ROLLOVER | IR_OC3;

    regs->ocr[2] = TICK_100MS;
    gpt_set_prescale (66);

    regs->control |= CR_EN;
    //printf ("timer started\n");

    return CLOCK_R_OK;
}

int
stop_timer(void)
{
    if (regs == NULL) {
        return CLOCK_R_UINT;
    } else if (!(regs->control & CR_EN)) {
        return CLOCK_R_CNCL;
    }

    // stop the clock first
    gpt_reset();

    // and free any registered timers
    while (timers) {
        struct timer_node* next = timers->next;
        free(timers);
        timers = next;
    }

    return CLOCK_R_OK;
}

int
register_timer(uint64_t delay, seL4_CPtr client)
{
    if (regs == NULL) {
        return CLOCK_R_UINT;
    } else if (!(regs->control & CR_EN)) {
        return CLOCK_R_CNCL;
    }

    struct timer_node* timer = create_timer (delay, client);
    if (!timer) {
        return CLOCK_R_FAIL;
    }

    //printf ("user asked for timer at %lld, scheduled for %lld\n", delay, timer->compare);
    insert_timer (timer);
    install_timers ();

    return CLOCK_R_OK;
}

timestamp_t
time_stamp(void)
{
    uint32_t counter = regs->counter;
    uint32_t status = regs->status;
    uint32_t local_overflows = overflows;

    // had an overflow we haven't handled yet
    if (status & SR_ROV) {
        local_overflows++;
        counter = regs->counter;
    }

    int64_t tt = ((uint64_t)(local_overflows) << 32) | (counter);
    return tt;
}
