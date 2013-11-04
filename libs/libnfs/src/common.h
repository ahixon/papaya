#ifndef __COMMON_H
#define __COMMON_H


/************************************************************
 *  Imports
 ***********************************************************/
extern void pawpaw_usleep(int usecs);
#define _usleep(us) pawpaw_usleep(us)

/* Mutex imports */
#include <sync/mutex.h>
#define _mutex                sync_mutex_t
#define _mutex_aquire(m)      sync_mutex(m)
#define _mutex_create()       sync_create_mutex()
#define _mutex_acquire(m)     sync_acquire(m)
#define _mutex_release(m)     sync_release(m)
#define _mutex_try_acquire(m) sync_try_acquire(m)
#define _mutex_destroy(m)     sync_destroy_mutex(m);


#endif /* __COMMON_H */
