/* @LICENSE(NICTA_CORE) */

/*
Author: Ben Leslie <benjl@cse.unsw.edu.au>
*/

#include <stdarg.h>
#include <stdio.h>
#include "format.h"

/* All of these functions do not lock the I/O stream. They all end up calling
 * format which handles the locking. */
 
int
printf(const char *format, ...)
{
	int ret;
	va_list ap;
	
	va_start(ap, format);
	ret = vfprintf(stdout, format, ap);
	va_end(ap);
	return ret;
}
