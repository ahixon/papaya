#ifndef _DEBUG_H_
#define _DEBUG_H_

#include <stdio.h>


void plogf(const char *msg, ...);

#define _dprintf(v, col, args...) \
            do { \
                if ((v) < verbose){ \
                    printf(col); \
                    plogf(args); \
                    printf("\033[0;0m"); \
                } \
            } while (0)

#define dprintf(v, ...) _dprintf(v, "\033[22;33m", __VA_ARGS__)

#define WARN(...) _dprintf(-1, "\033[1;31mWARNING: ", __VA_ARGS__)

#define NOT_IMPLEMENTED() printf("\033[22;34m %s:%d -> %s not implemented\n\033[;0m",\
                                  __FILE__, __LINE__, __func__);

#endif /* _DEBUG_H_ */
