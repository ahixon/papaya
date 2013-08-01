/* @LICENSE(NICTA_CORE) */

/*
  Author: Ben Leslie
  Description: Implementation of Pseudo-random numbers for libc (7.20.2)
  Note: These are *NOT* good random numbers. The algorithm used is
  straight from the ISOC99 specification.
*/
#include <stdlib.h>

unsigned long int _rand_next = 1;

/* not required to be thread safe by posix */
int
rand(void)
{
	_rand_next = _rand_next * 1103515245 + 12345;
	return (unsigned int) (_rand_next/65536) % 32768;
}
