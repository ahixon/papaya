#ifndef _EXECINFO_H
#define _EXECINFO_H
//#include <debug/execinfo.h>

extern int backtrace (void **__array, int __size);
//libc_hidden_proto (__backtrace)

//extern char **backtrace_symbols (void *const *__array, int __size);

//extern void backtrace_symbols_fd (void *const *__array, int __size,
//				    int __fd);
//libc_hidden_proto (__backtrace_symbols_fd)

#endif
