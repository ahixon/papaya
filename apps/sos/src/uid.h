#ifndef __PID_H__
#define __PID_H__

#include <sel4/sel4.h>
#include <sos.h>

#define PID_MAX		1024
#define PID_MIN		0
#define CBOX_MAX		(1 << 16)


void uid_init (void);

pid_t pid_next (void);
void pid_free (pid_t pid);

seL4_Word cid_next (void);
void cid_free (seL4_Word pid);

#endif