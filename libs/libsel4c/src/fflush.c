/* @LICENSE(NICTA_CORE) */
#include <stdio.h>

int
fflush(FILE *file)
{
	lock_stream(file);
	/* FIXME: printf("WARNING: fflush not implemented\n"); */
	unlock_stream(file);
	return 0;
}
