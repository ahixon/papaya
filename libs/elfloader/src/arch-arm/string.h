/* @LICENSE(NICTA_CORE) */

#ifndef _STRING_H_
#define _STRING_H_

#include "stdint.h"

int strcmp(const char *a, const char *b);
void *memset(void *s, int c, size_t n);
void *memcpy(void *dest, void *src, size_t n);

#endif
