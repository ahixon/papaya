/* @LICENSE(NICTA_CORE) */

/*
  Author: Ben Leslie
*/
#include <stdlib.h>

extern unsigned long int _rand_next;

void
srand(unsigned int seed)
{
	_rand_next = seed;
}
