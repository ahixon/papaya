/* @LICENSE(NICTA_CORE) */

/* Some helper functions to make TCB operations more sane. While it's possible
 * to optimise some of these functions to make them faster than the default
 * seL4_TCB_{Read|Write}Registers calls, this has deliberately been avoided in
 * order to avoid reasoning about a new set of kernel invocations. The primary
 * purpose of these is API convenience.
 */

#ifndef __LIBSEL4_TCB_H
#define __LIBSEL4_TCB_H

#include <interfaces/sel4_client.h>
#include <sel4/types.h>

#ifndef OFFSETOF
#ifdef __GNUC__
#define OFFSETOF(type, member) __builtin_offsetof(type, member)
#endif
#endif
#ifndef MAX
#define MAX(x, y) ((x) > (y) ? (x) : (y))
#endif

#ifndef compile_time_assert
#define compile_time_assert(name, expr) \
        typedef char _assertion_failed_##name[(expr) ? 1 : -1];
#endif
#ifdef OFFSETOF
/* seL4_TCB_ReadIPSP and seL4_TCB_WriteIPSP rely on EIP and ESP being the
 * first two registers in seL4_UserContext. If this is not the case, we will
 * not read/write enough registers in the syscall invocation.
 */
compile_time_assert(EIP_and_ESP_are_first_in_seL4_UserContext,
                    MAX(OFFSETOF(seL4_UserContext, eip), OFFSETOF(seL4_UserContext, esp))
                    == 1 * sizeof(seL4_Word));
#endif

static inline int
seL4_TCB_ReadIPSP(seL4_TCB service,
                  bool suspend_source,
                  seL4_Word *eip,
                  seL4_Word *esp)
{
    seL4_UserContext context = { 0 }; /* Initialise to avoid warnings. */
    int result;

    result = seL4_TCB_ReadRegisters(service, suspend_source, /* ignored */ 0,
                                    /* EIP and ESP */ 2, &context);

    *eip = context.eip;
    *esp = context.esp;

    return result;
}

static inline int
seL4_TCB_ReadAllRegisters(seL4_TCB service,
                          bool suspend_source,
                          seL4_UserContext *regs)
{
    return seL4_TCB_ReadRegisters(service, suspend_source, /* ignored */ 0,
                                  sizeof(seL4_UserContext) / sizeof(seL4_Word), regs);
}

static inline int
seL4_TCB_WriteIPSP(seL4_TCB service,
                   bool resume_target,
                   seL4_Word eip,
                   seL4_Word esp)
{
    seL4_UserContext context = {
        .eip = eip,
        .esp = esp,
    };

    return seL4_TCB_WriteRegisters(service, resume_target, /* ignored */ 0,
                                   /* EIP and ESP */ 2, &context);
}

static inline int
seL4_TCB_WriteAllRegisters(seL4_TCB service,
                           bool resume_target,
                           seL4_UserContext *regs)
{
    return seL4_TCB_WriteRegisters(service, resume_target, /* ignored */ 0,
                                   sizeof(seL4_UserContext) / sizeof(seL4_Word), regs);
}

#endif /* __LIBSEL4_TCB_H */
