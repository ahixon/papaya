/* @LICENSE(NICTA_CORE) */

/*
Author: Ben Leslie <benjl@cse.unsw.edu.au>
*/

#include <stdarg.h>
#include <stdio.h>
#include "format.h"

int
fprintf(FILE *stream, const char *format, ...)
{
	int ret;
	va_list ap;
	
	va_start(ap, format);
	ret = vfprintf(stream, format, ap);
	va_end(ap);
	return ret;
}

