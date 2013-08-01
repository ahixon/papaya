#ifndef _LIBOS_ELF_H_
#define _LIBOS_ELF_H_

#include <sel4/sel4.h>

int elf_load(seL4_ARM_PageDirectory dest_pd, char* elf_file);

#endif /* _LIBOS_ELF_H_ */
