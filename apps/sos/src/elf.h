#ifndef _LIBOS_ELF_H_
#define _LIBOS_ELF_H_

#include <sel4/sel4.h>
#include <vm/vm.h>

int load_segment_into_vspace(addrspace_t dest_as,
                                    seL4_CPtr src, unsigned long offset,
                                    unsigned long segment_size,
                                    unsigned long file_size, unsigned long dst,
                                    unsigned long permissions);

seL4_Word get_sel4_rights_from_elf(unsigned long permissions);

#endif /* _LIBOS_ELF_H_ */
