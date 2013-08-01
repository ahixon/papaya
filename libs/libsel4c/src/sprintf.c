/* @LICENSE(NICTA_CORE) */

/*
Author: Ben Leslie <benjl@cse.unsw.edu.au>
*/

#include <stdarg.h>
#include <stdio.h>
#include "format.h"

#include <assert.h>

int
sprintf(char *s, const char *format, ...)
{
	int ret;
	va_list ap;
	
	va_start(ap, format);
	ret = vsprintf(s, format, ap);
	va_end(ap);
	return ret;
}

int
vsprintf(char *s, const char *format, va_list arg)
{
	return format_string(s, NULL, 0, -1, format, arg);
}
