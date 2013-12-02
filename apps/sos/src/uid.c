#include <sel4/sel4.h>
#include <uid.h>
#include <stdio.h>
#include "ut_manager/bitfield.h"

static bitfield_t* pid_bitfield = NULL;
static bitfield_t* cbox_bitfield = NULL;

void uid_init (void) {
	/* FIXME: actually 2 << n rather than n ?? */
	cbox_bitfield = new_bitfield(CBOX_MAX, BITFIELD_INIT_EMPTY);
	pid_bitfield = new_bitfield(PID_MAX, BITFIELD_INIT_EMPTY);
}

pid_t pid_next (void) {
	pid_t next = bf_set_next_free (pid_bitfield);

	/* FIXME: do we actually need this? */
	pid_bitfield->next_free = next;

	return next;
}

void pid_free (pid_t pid) {
	if (pid > PID_MAX) {
		return;
	}

	bf_clr (pid_bitfield, pid);
}

seL4_Word cid_next (void) {
	seL4_Word next = bf_set_next_free (cbox_bitfield);

	/* FIXME: do we actually need this? */
	cbox_bitfield->next_free = next;

	return next;
}

void cid_free (seL4_Word cbox) {
	if (cbox > CBOX_MAX) {
		return;
	}

	bf_clr (cbox_bitfield, cbox);
}