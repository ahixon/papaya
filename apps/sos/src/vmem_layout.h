#ifndef _MEM_LAYOUT_H_
#define _MEM_LAYOUT_H_


#define DMA_VSTART          (0x10000000)
#define DMA_SIZE_BITS       (22)
#define DEVICE_START        (0xB0000000)

#define ROOT_VSTART         (0xC0000000)


#define PROCESS_STACK_TOP   (0x90000000)
#define PROCESS_IPC_BUFFER  (0xA0000000)
#define PROCESS_VMEM_START  (0xC0000000)

#define PROCESS_SCRATCH     (0xD0000000)


#endif /* _MEM_LAYOUT_H_ */
