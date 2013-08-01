.extern seL4_InitBootInfo
.extern seL4_GetBootInfo
.extern exit

.text

/*
 * Image Entry point.
 */
.global _start
_start:
    /* Setup a stack for ourselves. */
    ldr     sp, =_stack_top

    /* Setup bootinfo. The pointer to the bootinfo struct starts in 'r0'. */
    bl      seL4_InitBootInfo

    /* Call main. */
    bl      main
    b       exit

/*
 * Symbols required for libgcc.
 */
.global raise
//.global __aeabi_unwind_cpp_pr0
.global __aeabi_unwind_cpp_pr1
.global __aeabi_unwind_cpp_pr2
raise:
//__aeabi_unwind_cpp_pr0:
__aeabi_unwind_cpp_pr1:
__aeabi_unwind_cpp_pr2:
    b       raise

/* .text Literal Pool */
.pool

/* Stack for the image. */
.bss
.align  4
_stack_bottom:
.space  8192
_stack_top:

