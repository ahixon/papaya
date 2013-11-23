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

/**
 * Convert ELF permissions into seL4 permissions.
 */
seL4_Word get_sel4_rights_from_elf (unsigned long permissions) {
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
        printf ("%s: umm, file_size (0x%lx) > segment size (0x%lx)\n",
            __FUNCTION__, file_size, segment_size);
        return false;
    }

    struct as_region* reg = as_define_region (
        dest_as, dst, segment_size, permissions, REGION_GENERIC);

    if (!reg) {
        printf ("%s: failed to define region at 0x%lx\n", __FUNCTION__, dst);
        return false;
    }

    unsigned long pos;

    pos = 0;
    while (pos < file_size) {
        seL4_Word vpage = PAGE_ALIGN (dst);

        struct pt_entry* page = page_fetch_new (
            dest_as, reg->attributes, dest_as->pagetable, vpage);

        if (!page) {
            printf ("%s: failed to fetch page for vaddr 0x%x\n",
                __FUNCTION__, vpage);

            return false;
        }

        page->frame = frame_new ();
        if (!page->frame) {
            printf ("%s: failed to setup new page\n", __FUNCTION__);
            return false;
        }

        /* mmap the page to the file */
        int nbytes = PAGESIZE - (dst & PAGEMASK);

        page->frame->file = frame_create_mmap (
            src, dst - vpage, offset, MIN (nbytes, file_size - pos));
        printf ("mapping to file EP %d (%d) = %p\n", src, page->frame->file->file, page->frame->file);

        assert (page->frame->file);

        offset += nbytes;
        pos += nbytes;
        dst += nbytes;
    }

    return true;
}