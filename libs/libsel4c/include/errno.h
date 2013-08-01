/* @LICENSE(NICTA_CORE) */

/*
 Authors: Ben Leslie
 Description:
  Errors as per 7.5
 Status: Complete
 Restrictions: Single threaded
*/

#ifndef _ERRNO_H_
#define _ERRNO_H_

#define EDOM   1
#define EILSEQ 2
#define ERANGE 3
#define EIO    5
#ifndef THREAD_SAFE
extern int errno;
#else
#include <l4/thread.h>
 
#define errno (*((int *)__L4_TCR_ThreadLocalStorage()))
#endif

#endif /* _ERRNO_H_ */

