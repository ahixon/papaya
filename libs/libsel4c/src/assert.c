/* @LICENSE(NICTA_CORE) */

/*
  Authors: Ben Leslie
*/

#include <assert.h> /* For __assert */
#include <stdio.h> /* For fprintf() */
#include <stdlib.h> /* For abort() */

void
__assert(const char *expression, const char *file, 
	 const char *function, int line)
{
	/* Formatting as per suggestion in C99 spec 7.2.1.1 */
	fprintf(stderr, "Assertion failed: %s, function %s, "
		"file %s, line %d.\n", expression, function, file, line);
	abort();
}
