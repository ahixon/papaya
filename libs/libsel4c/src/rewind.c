/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

void
rewind(FILE *stream)
{
	/* This function is not locked as fseek will handle the locking. */
	(void) fseek(stream, 0L, SEEK_SET);
}
