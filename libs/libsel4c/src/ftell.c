/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

long int
ftell(FILE *stream)
{
	int res;
	lock_stream(stream);
	res = stream->current_pos;
	unlock_stream(stream);
	return res;
}
