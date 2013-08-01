/* @LICENSE(NICTA_CORE) */

#include <stdlib.h>

int
system(const char *string)
{
	/* Don't suppport a command interpreter at the moment */
	return 0;
}
