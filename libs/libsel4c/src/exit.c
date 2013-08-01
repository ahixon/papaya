/* @LICENSE(NICTA_CORE) */

/*
  Author: Ben Leslie
  Created: Wed Oct  6 2004 
*/
#include <stdlib.h>

void
exit(int status)
{
	/* Call registered atexit() functions */

	/* Flush unbuffered data */

	/* Close all streams */

	/* Remove anything created by tmpfile() */

	/* Call _Exit() */
	_Exit(status);
}

