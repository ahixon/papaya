#include "debug.h"

void plogf(const char *msg, ...) {
    va_list alist;

    va_start(alist, msg);
    vprintf(msg, alist);
    va_end(alist);
}
