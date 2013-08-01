#ifndef __LEGACY_DESC_H
#define __LEGACY_DESC_H

#include <ethdrivers/raw_iface.h>

/**
 * Handles legacy descriptors
 * Currently only allows get/put operations on the next buffer such that
 * "get" will always return the same buffer until a successful call to "put"
 * is made. It should be a trivial extension to correct this if needed.
 *
 * @Author Alexander Kroh
 */

struct ldesc;

struct ldesc_data {
    uint32_t tx_phys;
    uint32_t tx_bufsize;
    uint32_t rx_phys;
    uint32_t rx_bufsize;
};

/**
 * Create and initialise legacy descriptors 
 * @param[in] rx_count   Number of RX descriptors and buffers to create
 * @param[in] rx_bufsize The size of each RX buffer
 * @param[in] tx_count   Number of TX descriptors and buffers to create
 * @param[in] tx_bufsize The size of each TX buffer
 * @return               A reference to the legacy descriptor structure created.
 */
struct ldesc* ldesc_init(int rx_count, int rx_bufsize,
                         int tx_count, int tx_bufsize);

/**
 * Returns HW relevant data for legacy descriptors 
 * @param[in] ldesc  A reference to legacy descriptors
 * @retun            Physical address of the rings and their associated 
 *                   buffer sizes.
 */
struct ldesc_data ldesc_get_ringdata(struct ldesc* ldesc);


/**
 * Resets ring buffers. All indexes will begin again from 0.
 * @param[in] ldesc  A reference to legacy descriptor structure.
 */
void ldesc_reset(struct ldesc* ldesc);

/**
 * Retrieve an available TX buffer. NOTE: subsequent calls will receive the
 * same buffer until @ref ldesc_txput is successfully called.
 * @param[in]  ldesc  A reference to legacy descriptor structure
 * @param[out] dma    If successful, returns a representation of the next 
 *                    available buffer.
 * @return            The size of the buffer, or 0 if no buffers are available.
 */
int ldesc_txget(struct ldesc* ldesc, dma_addr_t* dma);

/**
 * Return a TX buffer back into the ring 
 * @param[in]  ldesc  A reference to legacy descriptor structure
 * @param[in]  buf    The buffer to insert. The buffer will not
 *                    be added to the internal pool and can therefore have an
 *                    arbitrary size.
 *                    the descriptor structure was created.
 * @param[in]  len    The length of the provided frame.
 * @return            0 indicates success
 */
int ldesc_txput(struct ldesc* ldesc, dma_addr_t buf, int len);

/**
 * Retrieve a filled RX buffer. NOTE: subsequent calls will receive the same
 * buffer until rx_put is called.
 * @param[in]  ldesc  A reference to legacy descriptor structure
 * @param[out] dma    If successful, returns a representation of a received frame.
 * @param[out] len    The length of the received frame.
 * @return            0 if there the queue is empty
 *                    1 if buf contains a valid frame.
 *                   -1 if buf contains an error frame
 */
int ldesc_rxget(struct ldesc* ldesc, dma_addr_t* dma, int* len);

/**
 * Return a RX buffer back into the ring 
 * @param[in]  ldesc  A reference to legacy descriptor structure
 * @param[in]  buf    The buffer to insert. If this is a newly allocated buffer,
 *                    its size must be compatible with the size specified when
 *                    the descriptor structure was created.
 */
void ldesc_rxput(struct ldesc* ldesc, dma_addr_t dma);

/**
 * Cycles through and cleans up empty TX descriptors  At the least, this
 * function must report on whether or not there are descriptors waiting
 * to be sent.
 * @param[in]  ldesc  A reference to legacy descriptor structure
 * @return            1 if there are no more descriptors pending
 */
int ldesc_txcomplete(struct ldesc* ldesc);

/* Print descriptors */
void ldesc_print(struct ldesc* ldesc);

#endif /* __LEGACY_DESC_H */
