/* @LICENSE(NICTA_CORE) */

/*
  Author: Ben Leslie
*/
#ifndef _FORMAT_H_
#define _FORMAT_H_
#include <stdarg.h>
#include <stdbool.h>
int format_string(char *output, FILE *stream, bool stream_or_memory, size_t n, const char *fmt, va_list ap);
#endif
