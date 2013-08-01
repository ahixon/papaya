/* @LICENSE(NICTA_CORE) */

/*
  Author: Ben Leslie
*/

#include <stdlib.h>
#include <string.h>

void *
calloc(size_t nmemb, size_t size)
{
	void *ptr;
	ptr = malloc(nmemb * size);
	if (ptr)
		memset(ptr, '\0', nmemb * size);
	return ptr;
}
