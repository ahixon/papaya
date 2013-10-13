#include <thread.h>
#include <vm/vm.h>

char* copyin (thread_t thread, vaddr_t ubuf, unsigned int usize, char* kbuf, unsigned int ksize);