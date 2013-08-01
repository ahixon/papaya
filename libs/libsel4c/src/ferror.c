/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

int
ferror(FILE *stream)
{
	int res;
	lock_stream(stream);
	res = stream->error;
	unlock_stream(stream);
	return res;
}
