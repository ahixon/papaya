/* @LICENSE(NICTA_CORE) */

/*
Authors: Ben Leslie
*/

#include <stdio.h>

int
putchar(int c)
{
	return fputc(c, stdout);
}
