#ifndef __SERVICES_H__
#define __SERVICES_H__

#include <sel4/sel4.h>
#include <uid.h>

int mapper_main (void);
int mmap_main (void);
int fs_cpio_main (void);

extern short badgemap_found;
void maps_append (seL4_Word idx, pid_t pid, vaddr_t start);
	
#endif