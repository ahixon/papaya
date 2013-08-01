/* @LICENSE(NICTA_CORE) */

/*
Author: Ben Leslie <benjl@cse.unsw.edu.au>
*/

#include <stdio.h>
#include "format.h"

int
vfprintf(FILE *stream, const char *format, va_list arg)
{
	return format_string(NULL, stream, 1, -1, format, arg);
}
