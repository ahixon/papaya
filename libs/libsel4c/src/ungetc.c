/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

int
ungetc(int c, FILE *stream)
{
	/* 
	   XXX: Note this isn't a full implementation of ungetc, and in particular
	   doesn't have the correct semantics for binary streams
	*/
	if (c == EOF) {
		return c;
	}
	if (stream->unget_pos < __UNGET_SIZE) {
		stream->eof = 0;
		stream->unget_stack[stream->unget_pos++] = c;
		return c;
	} else {
		return EOF;
	}
}
