/* @LICENSE(NICTA_CORE) */

#ifndef _STDIO_H_
#define _STDIO_H_

#define NULL ((void *)0)
#define FILE void

/* Architecture-specific putchar implementation. */
int __fputc(int c, FILE *data);

int printf(const char *format, ...);
int sprintf(char *buff, const char *format, ...);

#endif
