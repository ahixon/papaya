#ifndef __BADGEMAP_H__
#define __BADGEMAP_H__

#include <sel4/sel4.h>
#include <uid.h>

extern short badgemap_found;

int mapper_main (void);
void maps_append (seL4_Word idx, pid_t pid, vaddr_t start);
	
#endif