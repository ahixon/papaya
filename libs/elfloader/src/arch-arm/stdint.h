/* @LICENSE(NICTA_CORE) */

#ifndef _STDINT_H
#define _STDINT_H

typedef signed char int8_t;
typedef short int16_t;
typedef int int32_t;
typedef long long int64_t;

typedef unsigned char uint8_t;
typedef unsigned short uint16_t;
typedef unsigned int uint32_t;
typedef unsigned long long uint64_t;

typedef uint32_t uintptr_t;
typedef uint32_t size_t;

#define UINT64_MAX (18446744073709551615ULL)

#define __PTR_SIZE 32
#endif
