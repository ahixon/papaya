/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include <assert.h>

int
fseek(FILE *stream, long int offset, int whence)
{
	int res = 0;
	lock_stream(stream);
	switch(whence) {
	case SEEK_SET:
		stream->current_pos = offset;
		break;
	case SEEK_CUR:
		stream->current_pos += offset;
		break;
	case SEEK_END:
		stream->current_pos = stream->eof_fn(stream->handle) + offset;
		break;
	default:
		res = -1;
	}
	unlock_stream(stream);
	return res;
}
