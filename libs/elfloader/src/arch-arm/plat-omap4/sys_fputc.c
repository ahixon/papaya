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

/* OMAP3 UART 2 physical address. */
#define UART_PADDR 0x48020000
#define UART_PPTR UART_PADDR

#define UTHR 0x00 /*UART Transmit Holding Register*/
#define ULSR 0x14 /*UART Line Status Register*/

#define UART_REG(x) ((volatile uint8_t *)(UART_PPTR+(x)))

#define UART_THRE 0x20 /*Transmit Holding Register Empty*/

int
__fputc(int c, FILE *stream)
{
    /* Wait until UART ready for the next character. */
    while ( ((*UART_REG(ULSR))&UART_THRE) == 0);

    /* Put in the register to be sent*/
    *UART_REG(UTHR) = c;

    /* Send '\r' after every '\n'. */
    if (c == '\n') {
        (void)__fputc('\r', stream);
    }

    return 0;
}

