#ifndef _CLOCK_H_
#define _CLOCK_H_

#include <sel4/sel4.h>

/*
 * Return codes for driver functions
 */
#define CLOCK_R_OK     0        /* success */
#define CLOCK_R_UINT (-1)       /* driver not initialised */
#define CLOCK_R_CNCL (-2)       /* operation cancelled (driver stopped) */
#define CLOCK_R_FAIL (-3)       /* operation failed for other reason */

typedef int64_t timestamp_t;

/*
 * Initialise driver. Performs implicit stop_timer() if already initialised.
 *    bi:       seL4 boot information and capabilities
 *
 * Returns CLOCK_R_OK iff successful.
 */
int start_timer(seL4_CPtr interrupt_ep);

/*
 * Register a client thread to be woken up by an IPC.
 *    delay:  Delay time in microseconds before sending a reply IPC to client.
 *    client: Client id.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int register_timer(uint64_t delay, seL4_CPtr client);

/*
 * Returns present time in microseconds since booting.
 *
 * Returns a negative value if failure.
 */
timestamp_t time_stamp(void);

/*
 * Stop clock driver operation.
 *
 * Returns CLOCK_R_OK iff successful.
 */
int stop_timer(void);

void handle_timer(void);

#endif /* _CLOCK_H_ */
