/* @LICENSE(NICTA_CORE) */

/*
Author: Ben Leslie <benjl@cse.unsw.edu.au>
*/

#include <stdarg.h>
#include <stdio.h>
#include "format.h"

int
vprintf(const char *format, va_list arg)
{
	return vfprintf(stdout, format, arg);
}

