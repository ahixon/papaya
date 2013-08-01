/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

extern FILE * sys_tmpfile(void);

FILE *
tmpfile(void)
{
	FILE *tmp = sys_tmpfile();
	if (tmp != NULL) {
		/* Here we need to do additional tests 
		   and setup */
	}
	return tmp;
}
