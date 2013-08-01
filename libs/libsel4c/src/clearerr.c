/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
 
void
clearerr(FILE *stream)
{
	lock_stream(stream);
	stream->error = 0;
	stream->eof = 0;
	unlock_stream(stream);
}
