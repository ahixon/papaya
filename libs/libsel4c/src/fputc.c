/* @LICENSE(UNKNOWN) */

#include <stdio.h>
#include <stdlib.h>
#include <assert.h>

#define BUF_SIZE	128
#define SEND_CHAR	'\n'

int
fputc(int c, FILE *stream)
{
	unsigned char ch = (unsigned char) c;

	/* This is where we should do output buffering */
	lock_stream(stream);

	if (stream->buffer == NULL) {
		stream->buffer = malloc (sizeof (char) * BUF_SIZE);
		/* malloc failed, just write one char and try again next call */
		if (!stream->buffer) {
			if (stream->write_fn(&ch, 0, 1, stream->handle))  {
				unlock_stream (stream);
				return c;
			} else {
				unlock_stream (stream);
				return EOF;
			}
		}
	}

	stream->buffer[stream->current_pos] = ch;
	if (ch == SEND_CHAR || stream->current_pos == (BUF_SIZE - 1)) {
		if (stream->write_fn(stream->buffer, 0, stream->current_pos + 1, stream->handle) > 0) {
			/* Success - wrote SOMETHING */
			/* FIXME: what if we don't manage to write everything */
			stream->current_pos = 0;
			unlock_stream(stream);
			return c;
		} else {
			unlock_stream(stream);
			return EOF;
		}
	} else {
		stream->current_pos++;
		unlock_stream (stream);
		return c;
	}
}