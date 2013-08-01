/* @LICENSE(NICTA_CORE) */

/*
Author: Ben Leslie
*/

#include <stdio.h>

size_t
fwrite(const void * ptr, size_t size, size_t nmemb, FILE * stream)
{
	size_t elems, sz;
	const unsigned char *p = ptr;

	lock_stream(stream);
	for(elems = 0; elems < nmemb; elems++) {
		for(sz = 0; sz < size; sz++, p++) {
			if (fputc(*p, stream) == EOF) {
				goto out;
			}
		}
	}
out:
	unlock_stream(stream);
	return elems;
}
