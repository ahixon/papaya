#ifndef __PID_H__
#define __PID_H__

#define PID_MAX		1024

typedef int pid_t;

void pid_init (void);

pid_t pid_next (void);
void pid_free (pid_t pid);

#endif