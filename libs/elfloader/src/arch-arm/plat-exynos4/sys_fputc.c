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

#define UART0_PADDR 0x13800000
#define UART1_PADDR 0x13810000
#define UART2_PADDR 0x13820000
#define UART3_PADDR 0x13830000
#define UART4_PADDR 0x13840000
#define UART_PADDR  (UART1_PADDR)

#define ULCON       0x0000 /* line control */
#define UCON        0x0004 /*control */
#define UFCON       0x0008 /* fifo control */
#define UMCON       0x000C /* modem control */
#define UTRSTAT     0x0010 /* TX/RX status */
#define UERSTAT     0x0014 /* RX error status */
#define UFSTAT      0x0018 /* FIFO status */
#define UMSTAT      0x001C /* modem status */
#define UTXH        0x0020 /* TX buffer */
#define URXH        0x0024 /* RX buffer */
#define UBRDIV      0x0028 /* baud rate divisor */
#define UFRACVAL    0x002C /* divisor fractional value */
#define UINTP       0x0030 /* interrupt pending */
#define UINTSP      0x0034 /* interrupt source pending */
#define UINTM       0x0038 /* interrupt mask */

#define UART_REG(x) ((volatile uint32_t *)(UART_PADDR + (x)))

/* ULCON */
#define WORD_LENGTH_8   (3<<0)

/* UTRSTAT */
#define TX_EMPTY        (1<<2)
#define TXBUF_EMPTY     (1<<1)

int
__fputc(int c, FILE *stream)
{
    /* Wait until UART ready for the next character. */
    while ( !(*UART_REG(UTRSTAT) & TXBUF_EMPTY) );

    /* Put in the register to be sent*/
    *UART_REG(UTXH) = (c & 0xff);

    /* Send '\r' after every '\n'. */
    if (c == '\n') {
        (void)__fputc('\r', stream);
    }

    return 0;
}

