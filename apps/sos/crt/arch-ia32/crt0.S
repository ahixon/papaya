    .global _start
    .extern test_Init
    .extern exit

    .text

_start:
    leal    _stack_top, %esp
    pushl   %ebx
    call    test_Init
    addl    $4, %esp
    call    main
    pushl   %eax
    call    exit
1:  jmp     1b

    .bss
    .align  4

_stack_bottom:
    .space  4096
_stack_top:
