
#ifndef _MAPPING_H_
#define _MAPPING_H_

#include <sel4/sel4.h>
#include <thread.h>

 /**
 * Maps a page into a page table. 
 * A 2nd level table will be created if required
 *
 * @param frame_cap a capbility to the page to be mapped
 * @param pd A capability to the page directory to map to
 * @param vaddr The virtual address for the mapping
 * @param rights The access rights for the mapping
 * @param attr The VM attributes to use for the mapping
 * @return 0 on success
 */
int map_page(seL4_CPtr frame_cap, seL4_ARM_PageDirectory pd, seL4_Word vaddr, 
                seL4_CapRights rights, seL4_ARM_VMAttributes attr);
 
 /**
 * Maps a device to virtual memory
 * A 2nd level table will be created if required
 *
 * @param paddr the physical address of the device
 * @param size the number of bytes that this device occupies
 * @return The new virtual address of the device
 */
void* map_device(void* paddr, int size);

void* map_device_thread(void* paddr, int size, thread_t thread);

#endif /* _MAPPING_H_ */
