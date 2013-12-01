#ifndef __SERVICES_H__
#define __SERVICES_H__

#include <sel4/sel4.h>
#include <uid.h>

#define MMAP_REQUEST 	1
#define MMAP_RESULT		2

#define MMAP_IRQ		1

int mapper_main (void);
int mmap_main (void);
int fs_cpio_main (void);

extern short badgemap_found;
void maps_append (seL4_Word idx, pid_t pid, vaddr_t start);

int mmap_swap (int direction, vaddr_t vaddr, struct frameinfo* frame, void* callback, struct pawpaw_event* evt);
	
#endif