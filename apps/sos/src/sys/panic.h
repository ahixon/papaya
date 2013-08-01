#ifndef PANIC_H
#define PANIC_H

#include <stdio.h>
/**
 * Panic if condition is true
 */
#define conditional_panic(a, b) __conditional_panic(a, b, __FILE__, __func__, __LINE__)
#define panic(b) conditional_panic(1, b)

void __conditional_panic(int condition, const char *message, const char *file,
        const char *func, int line);

#endif
