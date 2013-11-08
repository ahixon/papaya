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

#include <mapping.h>    // I SHOULD GO BECAUSE I SUCK

#define verbose 0
#include <sys/debug.h>
#include <sys/panic.h>

/* Minimum of two values. */
#define MIN(a,b) (((a)<(b))?(a):(b))

#define PAGESIZE              (1 << (seL4_PageBits))
#define PAGEMASK              ((PAGESIZE) - 1)
#define PAGE_ALIGN(addr)      ((addr) & ~(PAGEMASK))
#define IS_PAGESIZE_ALIGNED(addr) !((addr) &  (PAGEMASK))


extern seL4_ARM_PageDirectory dest_as;

/*
 * Convert ELF permissions into seL4 permissions.
 */
static inline seL4_Word get_sel4_rights_from_elf(unsigned long permissions) {
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
 * Inject data into the given vspace.
 */
static int load_segment_into_vspace(addrspace_t dest_as,
                                    char *src, unsigned long segment_size,
                                    unsigned long file_size, unsigned long dst,
                                    unsigned long permissions) {
    assert(file_size <= segment_size);

    unsigned long pos;

    if (!as_define_region (dest_as, dst, segment_size, permissions, REGION_GENERIC)) {
        return 1;
    }

    /* We work a page at a time in the destination vspace. */
    pos = 0;
    while(pos < segment_size) {
        seL4_CPtr sos_cap, frame_cap;
        seL4_Word vpage, kvpage;

        unsigned long kdst;
        int nbytes;
        int err;

        kdst   = dst + PROCESS_SCRATCH;
        vpage  = PAGE_ALIGN(dst);
        kvpage = PAGE_ALIGN(kdst);

        /* Map the page into the destination address space */
        struct pt_entry* page = as_map_page (dest_as, vpage);
        if (!page) {
            panic ("failed to map into process addrspace");
        }

        /* Map the frame into SOS as well so we can copy into it */
        /* FIXME: WOULD BE MUCH NICER(!) if we just used cur_addrspace - 
         * you will need to create a region in main's init function */
        //sos_cap = frametable_fetch_cap (frame);
        sos_cap = page->cap;
        conditional_panic (!sos_cap, "could not fetch cap from frametable");

        frame_cap = cspace_copy_cap (cur_cspace, cur_cspace, sos_cap, seL4_AllRights);
        if (!frame_cap) {
            printf ("%s: failed to copy cap\n", __FUNCTION__);
            return 1;
        }
        
        err = map_page (frame_cap, seL4_CapInitThreadPD, kvpage, seL4_AllRights, seL4_ARM_Default_VMAttributes);
        conditional_panic (err, "could not map into SOS");

        /* Now copy our data into the destination vspace */
        nbytes = PAGESIZE - (dst & PAGEMASK);
        if (pos < file_size){
            memcpy((void*)kdst, (void*)src, MIN(nbytes, file_size - pos));
        }

        /* Not observable to I-cache yet so flush the frame */
        seL4_ARM_Page_FlushCaches(frame_cap);

        /* unmap page + delete cap copy */
        err = seL4_ARM_Page_Unmap (frame_cap);     
        conditional_panic(err, "could not unmap from SOS");

        cspace_delete_cap (cur_cspace, frame_cap);

        pos += nbytes;
        dst += nbytes;
        src += nbytes;
    }

    return 0;
}

int elf_load(addrspace_t dest_as, char *elf_file) {

    int num_headers;
    int err;
    int i;

    /* Ensure that the ELF file looks sane. */
    if (elf_checkFile(elf_file)){
        return seL4_InvalidArgument;
    }

    num_headers = elf_getNumProgramHeaders(elf_file);
    for (i = 0; i < num_headers; i++) {
        char *source_addr;
        unsigned long flags, file_size, segment_size, vaddr;

        /* Skip non-loadable segments (such as debugging data). */
        if (elf_getProgramHeaderType(elf_file, i) != PT_LOAD)
            continue;

        /* Fetch information about this segment. */
        source_addr = elf_file + elf_getProgramHeaderOffset(elf_file, i);
        file_size = elf_getProgramHeaderFileSize(elf_file, i);
        segment_size = elf_getProgramHeaderMemorySize(elf_file, i);
        vaddr = elf_getProgramHeaderVaddr(elf_file, i);
        flags = elf_getProgramHeaderFlags(elf_file, i);

        /* Copy it across into the vspace. */
        dprintf(1, " * Loading segment %08x-->%08x\n", (int)vaddr, (int)(vaddr + segment_size));
        err = load_segment_into_vspace(dest_as, source_addr, segment_size, file_size, vaddr,
                                       get_sel4_rights_from_elf(flags) & seL4_AllRights);
        conditional_panic(err != 0, "Elf loading failed!\n");
    }

    err = as_create_stack_heap (dest_as, NULL, NULL);
    conditional_panic (err, "failed to create stack and heap\n");

    return 0;
}
