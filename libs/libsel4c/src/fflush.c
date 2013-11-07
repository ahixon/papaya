/* @LICENSE(NICTA_CORE) */
#include <stdio.h>

int
fflush(FILE *stream)
{
	lock_stream(stream);
	int res;
	
	if (stream->buffer) {
		res = stream->write_fn(stream->buffer, 0, stream->current_pos, stream->handle);
		if (res) {
			stream->current_pos = 0;
		}
	} else {
		res = 0;
	}

	unlock_stream(stream);
	return res;
}
