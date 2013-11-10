#include <sel4/sel4.h>
#include <elf/elf.h>
#include <string.h>
#include <assert.h>
#include <cspace/cspace.h>

#include "elf.h"

#include <ut_manager/ut.h>
#include <vm/vm.h>
#include <vm/addrspace.h>
#include <vm/frametable.h>
#include <vm/vmem_layout.h>

/* Minimum of two values. */
#define MIN(a,b) (((a)<(b))?(a):(b))

#define PAGESIZE              (1 << (seL4_PageBits))
#define PAGEMASK              ((PAGESIZE) - 1)
#define PAGE_ALIGN(addr)      ((addr) & ~(PAGEMASK))
#define IS_PAGESIZE_ALIGNED(addr) !((addr) &  (PAGEMASK))

/*
 * Convert ELF permissions into seL4 permissions.
 */
seL4_Word get_sel4_rights_from_elf(unsigned long permissions) {
    seL4_Word result = 0;

    if (permissions & PF_R)
        result |= seL4_CanRead;
    if (permissions & PF_X)
        result |= seL4_CanRead;
    if (permissions & PF_W)
        result |= seL4_CanWrite;

    return result;
}

/*
 * Mmaps some stuff to a given vspace.
 */
int load_segment_into_vspace(addrspace_t dest_as,
                                    seL4_CPtr src, unsigned long offset,
                                    unsigned long segment_size,
                                    unsigned long file_size, unsigned long dst,
                                    unsigned long permissions) {
    if (file_size > segment_size) {
        printf ("%s: umm, file_size (0x%lx) > segment size (0x%lx)\n", __FUNCTION__, file_size, segment_size);
        return false;
    }

    if (!as_define_region (dest_as, dst, segment_size, permissions, REGION_GENERIC)) {
        printf ("%s: failed to define region at 0x%lx\n", __FUNCTION__, dst);
        return false;
    }

    unsigned long pos;

    pos = 0;
    while (pos < segment_size) {
        seL4_Word vpage = PAGE_ALIGN (dst);

        struct pt_entry* page = page_fetch_entry (dest_as, seL4_ARM_Default_VMAttributes, dest_as->pagetable, vpage);
        if (!page) {
            printf ("%s: failed to fetch page for vaddr 0x%x\n", __FUNCTION__, vpage);
            return false;
        }

        page->frame = frame_new_from_untyped (0);
        if (!page->frame) {
            printf ("%s: failed to allocate new page\n", __FUNCTION__);
            return false;
        }

        printf ("elf: set frame info for 0x%x - file cap %d @ offset 0x%x\n", vpage, src, offset);

        /* mmap the page to the file */
        page->frame->file = src;
        page->frame->offset = offset;

        /* FIXME: increment by nbytes or PAGE_SIZE */
        //int nbytes = PAGESIZE - (dst & PAGEMASK);
        /*offset += nbytes;
        pos += nbytes;
        dst += nbytes;*/

        offset += PAGE_SIZE - 0x680;
        pos += PAGE_SIZE;
        dst += PAGE_SIZE;
    }

    return true;
}