#ifndef _LIBOS_ELF_H_
#define _LIBOS_ELF_H_

#include <sel4/sel4.h>
#include <frametable.h>
#include <addrspace.h>

int elf_load(addrspace_t dest_as, char *elf_file);

#endif /* _LIBOS_ELF_H_ */
