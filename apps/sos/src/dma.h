
#include <ethdrivers/raw_iface.h>

struct dma_addr sos_dma_malloc(void* cookie, uint32_t size, int cached);
void dma_invalidate(void* cookie, vaddr_t addr, int range);
void dma_clean(void* cookie, vaddr_t addr, int range);

