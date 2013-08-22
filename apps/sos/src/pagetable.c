/* 
 * two level page table
 * top 20 bits for physical address, bottom for bit flags
 * 
 * original cap stored in frame table
 * create copy, and map into process vspace
 * only delete this original cap on free-ing the frame
 * 
 * if we need to map into SOS, then we map into special window region, copy data in/out, then unmap - DO NOT DELETE 
 * 
 * on deletion of page, just revoke on original cap - this will NOT delete the cap, but all children
 * FIXME: need to check that page is not shared in future (otherwise those mapped pages will have their cap deleted too - basically dangling pointer)
 *
 * by itself: cap for page directory
 * first section of pagetable: pointers to second level
 * second part:                pointers to seL4 page table objects
 * ^ REMEMBER TO HAVE THIS ALL IN FRAMES
 *
 * second level: as above, 20 bit physical addr/index + bit flags
 *
 * might be better to have second level pointer then page table object next to each other for cache access
 */