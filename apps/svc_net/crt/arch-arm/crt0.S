# @LICENSE(NICTA_CORE)

.extern seL4_InitBootInfo
.extern exit

.text

/*
 * Image Entry point.
 */
.global _start
_start:
    /* Call main. */
    bl      main
    b       exit

/*
 * Symbols required for libgcc.
 */
.global raise
.global __aeabi_unwind_cpp_pr0
.global __aeabi_unwind_cpp_pr1
.global __aeabi_unwind_cpp_pr2
raise:
__aeabi_unwind_cpp_pr0:
__aeabi_unwind_cpp_pr1:
__aeabi_unwind_cpp_pr2:
    b       raise

/* .text Literal Pool */
.pool

