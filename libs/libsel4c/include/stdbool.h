/* @LICENSE(NICTA_CORE) */

/*
 Author: Ben Leslie
 Description:
   Implementation based on C99 Section 7.16 Boolean type and values
*/

#ifndef __STDBOOL_H__
#define __STDBOOL_H__

#ifndef __cplusplus

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

#endif /* __cplusplus */

#endif /* __STDBOOL_H__ */
