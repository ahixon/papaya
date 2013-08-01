/* @LICENSE(NICTA_CORE) */

#include <stdio.h>

int
feof(FILE *f)
{
	int res;
	lock_stream(f);
	res = f->eof;
	unlock_stream(f);
	return res;
}
