#include <stdio.h>
#include <sel4/sel4.h>

#include "ttyout.h"

struct __file __stdin = {
	0,
	sos_read,
	NULL,
	NULL,
	NULL,
	_IONBF,
	NULL,
	0,
	0
};


struct __file __stdout = {
	(void*)1,
	NULL,
	sos_write,
	NULL,
	NULL,
	_IONBF,
	NULL,
	0,
	0
};


struct __file __stderr = {
	(void*)2,
	NULL,
	sos_write,
	NULL,
	NULL,
	_IONBF,
	NULL,
	0,
	0
};

FILE *stdin = &__stdin;
FILE *stdout = &__stdout;
FILE *stderr = &__stderr;
