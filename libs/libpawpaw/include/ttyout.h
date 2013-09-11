#ifndef _TTYOUT_H
#define _TTYOUT_H

#include <stdio.h>

/* Print to the proper console.  You will need to finish these implementations */
extern size_t
sos_write(void *data, long int position, size_t count, void *handle);
extern size_t
sos_read(void *data, long int position, size_t count, void *handle);

/* Called when a process ends */
extern void 
abort(void);

#endif
