/* @LICENSE(NICTA_CORE) */

#ifndef __LIBSEL4_MACROS_H
#define __LIBSEL4_MACROS_H

/*
 * Some compilers attempt to pack enums into the smallest possible type.
 * For ABI compatability with the kernel, we need to ensure they remain
 * the same size as an 'int'.
 */
#define SEL4_FORCE_LONG_ENUM(type) \
        _enum_pad_ ## type = (1 << ((sizeof(int)*8) - 1))

#define CONST        __attribute__((__const__))
#define PURE         __attribute__((__pure__))

#endif
