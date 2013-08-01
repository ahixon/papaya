/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

size_t
fread(void *ptr, size_t size, size_t nmemb, FILE *stream)
{
	size_t elems, sz;
	unsigned char *p = ptr;

	lock_stream(stream);
	for(elems = 0; elems < nmemb; elems++) {
		for(sz = 0; sz < size; sz++, p++) {
			int ch;
			if ((ch = fgetc(stream)) == EOF) {
				goto out;
			}
			*p = (unsigned char) ch;
		}
	}
out:
	unlock_stream(stream);
	return elems;
}
