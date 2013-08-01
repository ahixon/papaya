/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include <stdlib.h>

int
fclose(FILE *stream)
{
	int r;
	lock_stream(stream);
	r = stream->close_fn(stream->handle);
	fflush(stream);
	unlock_stream(stream);
	return r;
}
