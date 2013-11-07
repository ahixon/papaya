/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include <stdlib.h>

int
fclose(FILE *stream)
{
	int r;
	lock_stream(stream);
	fflush(stream);
	r = stream->close_fn(stream->handle);
	unlock_stream(stream);
	return r;
}
