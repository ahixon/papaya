/* @LICENSE(NICTA_CORE) */

/*
  Author: Ben Leslie, Alex Webster
*/
#ifndef _SETJMP_H_
#define _SETJMP_H_

#include <arch/setjmp.h>

int setjmp(jmp_buf);
void longjmp(jmp_buf, int);

#endif /* _SETJMP_H_ */
