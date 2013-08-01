/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include "format.h"

int
vsnprintf(char *str, size_t size, const char *format, va_list arg)
{
	if (size == 0) {
		return 0;
	}
	return format_string(str, NULL, 0, size - 1, format, arg);
}
