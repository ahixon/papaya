/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include <string.h>

int
fputs(const char *s, FILE *stream)
{
	int c, len = strlen(s);
	/* This function does not need to lock the stream as fwrite will
	 * handle the locking. */
	c = fwrite(s, 1, len, stream);
	if (c < len) {
		return EOF;
	} else {
		return 1;
	}
}
