/* @LICENSE(NICTA_CORE) */

/*
 Authors: Ben Leslie
 Created: Wed Aug  4 13:37:35 EST 2004
*/

#include <string.h>

/*
 * return pointer to first occurrence of c in s 
 */
/* THREAD SAFE */
void *
memchr(const void *s, int c, size_t n)
{
	const char *p = (const char *) s;

	while (n--) {
		if (*p == c) {
			return (void *) p;
		}
		p++;
	}

	return NULL;
}
