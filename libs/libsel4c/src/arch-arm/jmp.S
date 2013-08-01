/* @LICENSE(NICTA_CORE) */

#include "asm.h"

#ifdef __thumb__
#define	ret  bx
#else
#define	ret  mov    pc, 
#endif

BEGIN_PROC(setjmp)
	stmia	r0, {r4-r11,r13,r14}
	mov	r0, #0
	ret	lr
END_PROC(setjmp)

BEGIN_PROC(longjmp)
	ldmia	r0, {r4-r11,r13,r14}
	movs	r0, r1
	moveq	r0, #1            /* don't return zero */
	ret	lr
END_PROC(longjmp)
