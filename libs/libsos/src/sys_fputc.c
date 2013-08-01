/* @LICENSE(NICTA_CORE) */

/*
  Author: Philip Derrin
  Created: Wed Jan 25 2006 
*/

#include <stdio.h>
#include <sel4/sel4.h>

int
fputc(int c, FILE *stream)
{
	seL4_DebugPutChar(c);
	return 0;
}
