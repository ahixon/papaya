/* @LICENSE(NICTA_CORE) */

/*
  Authors: Carl van Schaik, National ICT Australia
*/

#include <string.h>

/*
 * search for last occurrence of c in s 
 */
char *
strrchr(const char *s, int c)
{
	char *r = NULL;

	if( c != '\0') {
		while (*s != '\0') {
			if (*s++ == c)
				r = (char *)s - 1;
		}
	} else {
		r = (char *)s + strlen(s);
	}
	
	return r;
}
