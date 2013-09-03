#include <sel4/sel4.h>
#include <pid.h>
#include "ut_manager/bitfield.h"

static bitfield_t* pid_bitfield = NULL;

void pid_init (void) {
	pid_bitfield = new_bitfield(PID_MAX, BITFIELD_INIT_EMPTY);
}

pid_t pid_next (void) {
	pid_t next = bf_set_next_free(pid_bitfield);
	pid_bitfield->next_free = next;		/* FIXME: do we actually need this? */

	return next;
}

void pid_free (pid_t pid) {
	if (pid > PID_MAX) {
		return;
	}

	bf_clr (pid_bitfield, pid);
}