/*
 * Copyright (c) 2026 Beara.ai
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef BGT60TR13C_PLATFORM_H_
#define BGT60TR13C_PLATFORM_H_

#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <stdint.h>
#include <stddef.h>
#include "xensiv_bgt60trxx_platform.h"

struct bgt60tr13c_iface {
	struct spi_dt_spec spi;
	struct gpio_dt_spec reset_gpio;
	/* No cs_gpio: CS is handled by the Zephyr SPI framework */

	/* FIFO data goes straight to the caller's buffer,
	 * nothing here scales with the read size. The SPI controller works
	 * through these until an asynchronous FIFO read ends, so they live
	 * here rather than on the stack.
	 */
	uint8_t burst_cmd[4] __aligned(4);
	uint8_t burst_status[4] __aligned(4);
	struct spi_buf burst_tx[2];
	struct spi_buf burst_rx[2];
	struct spi_buf_set burst_tx_set;
	struct spi_buf_set burst_rx_set;
};

/*
 * Start reading num_samples samples (even) from the BGT60TR13C's FIFO into
 * dst as the FIFO stores them: 12-bit values packed two to every three
 * bytes, num_samples * 3 / 2 bytes total. A NULL dst reads and discards
 * them.
 *
 * Returns once the read has started, without waiting for it. cb is then
 * called when it ends, normally from the SPI controller's interrupt; pass
 * its result to bgt60tr13c_fifo_read_status(). Until then dst and bi are
 * the controller's. Returns a negative errno if the read couldn't be
 * started, and cb isn't called.
 */
int bgt60tr13c_fifo_read_async(struct bgt60tr13c_iface *bi, uint8_t *dst,
			       uint32_t num_samples, spi_callback_t cb, void *userdata);

/*
 * How a FIFO read went, given the SPI result it ended with: an
 * XENSIV_BGT60TRXX_STATUS_* code, which also reflects the GSR0 status the
 * radar sent back during the read.
 */
int32_t bgt60tr13c_fifo_read_status(const struct bgt60tr13c_iface *bi, int spi_result);

#endif /* BGT60TR13C_PLATFORM_H_ */
