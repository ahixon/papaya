/* @LICENSE(NICTA_CORE) */

/*
  Author: Alex Webster
 */
#include "asm.h"

#ifdef X86_64

/* 
 * The jmp_buf contains the following registers 
 * %rbx, %rsp, %rbp, %r12, %r13, %r14, %r15, return addr
 * the %rdi has the address of the jmp_buf and
 * the %rsp points to the return address
 */
BEGIN_PROC(setjmp)
    pop     %rsi                /* adjust the stack */
    movq    %rbx, 0(%rdi)
    movq    %rsp, 8(%rdi)
    push    %rsi
    movq    %rbp, 16(%rdi)
    movq    %r12, 24(%rdi)
    movq    %r13, 32(%rdi)
    movq    %r14, 40(%rdi)
    movq    %r15, 48(%rdi)
    movq    %rsi, 56(%rdi)
    xorq    %rax, %rax
    ret
END_PROC(setjmp)

BEGIN_PROC(longjmp)
    movq    %rsi, %rax
    movq    (%rdi), %rbx
    movq    8(%rdi), %rsp
    movq    16(%rdi), %rbp
    movq    24(%rdi), %r12
    movq    32(%rdi), %r13
    movq    40(%rdi), %r14
    movq    48(%rdi), %r15
    jmp     *56(%rdi)
END_PROC(longjmp)

#else

BEGIN_PROC(setjmp)
        movl 4(%esp), %ecx
        movl %ebx, 0(%ecx)
        movl %esi, 4(%ecx)
        movl %edi, 8(%ecx)
        movl %ebp, 12(%ecx)
        leal 4(%esp), %edx
        movl %edx, 16(%ecx)
        movl 0(%esp), %edx
        movl %edx, 20(%ecx)
        xor  %eax, %eax
        ret
END_PROC(setjmp)

BEGIN_PROC(longjmp)
        movl 4(%esp), %ecx
        movl 8(%esp), %eax
        cmpl $0, %eax            /* don't return zero */
        jne  1f
        movl $1, %eax
1:      movl 0(%ecx), %ebx
        movl 4(%ecx), %esi
        movl 8(%ecx), %edi
        movl 12(%ecx), %ebp
        movl 16(%ecx), %esp
        movl 20(%ecx), %edx
        jmp  *%edx
END_PROC(longjmp)

#endif
