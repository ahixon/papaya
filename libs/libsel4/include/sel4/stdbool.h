/* @LICENSE(NICTA_CORE) */

#ifndef __LIBSEL4_STDBOOL_H
#define __LIBSEL4_STDBOOL_H

/* Minimal bool definitions for the syscall stubs. */

#if !defined(__bool_true_false_are_defined) || __bool_true_false_are_defined != 1
#undef __bool_true_false_are_defined
#define __bool_true_false_are_defined 1
typedef _Bool bool;
#endif

#ifndef true
#define true 1
#endif

#ifndef false
#define false 0
#endif

#endif
