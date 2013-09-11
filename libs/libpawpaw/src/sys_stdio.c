/* @LICENSE(NICTA_CORE) */

#include <stdio.h>
#include <sel4/sel4.h>

#include "ttyout.h"



struct __file __stdin = {
	NULL,
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
	NULL,
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
	NULL,
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
