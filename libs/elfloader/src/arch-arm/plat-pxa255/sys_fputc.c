/* @LICENSE(NICTA_CORE) */

/*
 * Platform-specific putchar implementation.
 */

#include "../stdint.h"
#include "../stdio.h"

/*
 * Place a character to the given stream, which we always assume to be
 * 'stdout'.
 */
extern int
__fputc(int c, FILE *stream);

/* integratorCP UART physical address. */
#define UART_PADDR 0x40100000
#define UART_PPTR UART_PADDR

#define URXD  0x00 /* UART Receiver Register */
#define URST  0x14 /* status register */

#define UART_REG(x) ((volatile uint32_t *)(UART_PPTR+(x)))
#define BIT(x) (1 << (x))

int
__fputc(int c, FILE *stream)
{
    /* Wait until UART ready for the next character. */

    while (!((*UART_REG(URST) & BIT(6))));

    /* Add character to the buffer. */
    *UART_REG(URXD) = c;

    /* Send '\r' after every '\n'. */
    if (c == '\n') {
        (void)__fputc('\r', stream);
    }

    return 0;
}
