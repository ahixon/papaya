/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

int
fgetc(FILE *stream)
{
	unsigned char ch;

	lock_stream(stream);
	if (stream->unget_pos) {
		ch = stream->unget_stack[--stream->unget_pos];
		unlock_stream(stream);
		return (int) ch;
	}

	/* This is where we should do input buffering */
	if (stream->read_fn(&ch, stream->current_pos, 1, stream->handle) == 1) {
		/* Success */
		stream->current_pos++;
		unlock_stream(stream);
		return (int) ch;
	} else {
		stream->eof = 1;
		unlock_stream(stream);
		return EOF;
	}
}
