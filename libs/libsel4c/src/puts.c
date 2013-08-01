/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

int
puts(const char *s)
{
	while(*s != '\0')
		fputc(*s++, stdout);
	fputc('\n', stdout);
	return 0;
}
