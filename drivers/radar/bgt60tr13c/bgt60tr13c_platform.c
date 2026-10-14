/*
 * Copyright (c) 2026 Beara.ai
 * SPDX-License-Identifier: Apache-2.0
 *
 * Implements the platform functions declared in the vendor library's
 * xensiv_bgt60trxx_platform.h, using Zephyr's spi.h / gpio.h instead of
 * the PSoC6 HAL the upstream xensiv_bgt60trxx_mtb.c targets.
 *
 * Byte-oriented SPI throughout, deliberately: the vendor's PSoC6
 * implementation switches the SCB peripheral to a 12-bit word width for
 * the FIFO burst read (2 ADC samples pack into 3 bytes / 24 bits), but
 * not every Zephyr SPI controller driver supports word sizes other than
 * 8. The driver keeps the FIFO's packed bytes as they are (see
 * bgt60tr13c_fifo_read_async()); only the library's own
 * xensiv_bgt60trxx_get_fifo_data() unpacks them, in place.
 *
 * Register accesses wait for the bus. The driver's FIFO reads don't: they
 * use the SPI controller's asynchronous API (spi_transceive_cb()).
 */

#include "bgt60tr13c_platform.h"
#include "xensiv_bgt60trxx.h" /* vendor/ - for XENSIV_BGT60TRXX_STATUS_*,
				* XENSIV_BGT60TRXX_FIFO_WORD_SIZE_BYTES,
				* XENSIV_BGT60TRXX_NUM_SAMPLES_FIFO_WORD */

#include <zephyr/kernel.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/sys/byteorder.h>

void xensiv_bgt60trxx_platform_rst_set(const void *iface, bool val)
{
	const struct bgt60tr13c_iface *bi = iface;

	/* RSTN is active-low on the BGT60TR13C; reset-gpios in the
	 * devicetree should be GPIO_ACTIVE_LOW so gpio_pin_set_dt(..., 1)
	 * here means "assert reset" and matches the xensiv call sites
	 * (rst_set(true) during the hard-reset pulse in bgt60tr13c_init()).
	 */
	(void)gpio_pin_set_dt(&bi->reset_gpio, val ? 1 : 0);
}

void xensiv_bgt60trxx_platform_spi_cs_set(const void *iface, bool val)
{
	/* No-op: in Zephyr, the SPI controller driver owns CS, asserting it
	 * for the length of each spi_transceive(). The library drives CS
	 * around each register access, a single transfer either way, and to
	 * hold it across a FIFO burst's command and data, which the vendored
	 * library's local change sends as one transfer instead.
	 */
	ARG_UNUSED(iface);
	ARG_UNUSED(val);
}

void xensiv_bgt60trxx_platform_delay(uint32_t ms)
{
	k_msleep(ms);
}

uint32_t xensiv_bgt60trxx_platform_word_reverse(uint32_t x)
{
	return sys_cpu_to_be32(x);
}

void xensiv_bgt60trxx_platform_assert(bool expr)
{
	__ASSERT(expr, "xensiv_bgt60trxx assertion failed");
	ARG_UNUSED(expr); /* keep release builds quiet if asserts are off */
}

int32_t xensiv_bgt60trxx_platform_spi_transfer(void *iface, uint8_t *tx_data, uint8_t *rx_data,
						uint32_t len)
{
	struct bgt60tr13c_iface *bi = iface;

	/* NULL tx_data: send zeros. NULL rx_data: discard what comes back. */
	struct spi_buf tx_buf = {.buf = tx_data, .len = len};
	struct spi_buf_set tx_set = {.buffers = &tx_buf, .count = 1};

	struct spi_buf rx_buf = {.buf = rx_data, .len = len};
	struct spi_buf_set rx_set = {.buffers = &rx_buf, .count = 1};

	int ret = spi_transceive_dt(&bi->spi, &tx_set, &rx_set);

	return (ret == 0) ? XENSIV_BGT60TRXX_STATUS_OK : XENSIV_BGT60TRXX_STATUS_COM_ERROR;
}

/* Lay out a FIFO burst in bi for the SPI controller: send cmd (already
 * big-endian) while the radar returns its status word, then clock in
 * payload_len bytes to dst, or discard them if dst is NULL.
 */
static void bgt60tr13c_fifo_burst_setup(struct bgt60tr13c_iface *bi, uint32_t cmd,
					uint8_t *dst, size_t payload_len)
{
	memcpy(bi->burst_cmd, &cmd, sizeof(bi->burst_cmd));

	bi->burst_tx[0] = (struct spi_buf){.buf = bi->burst_cmd, .len = sizeof(bi->burst_cmd)};
	bi->burst_tx[1] = (struct spi_buf){.buf = NULL, .len = payload_len};
	bi->burst_rx[0] = (struct spi_buf){.buf = bi->burst_status,
					   .len = sizeof(bi->burst_status)};
	bi->burst_rx[1] = (struct spi_buf){.buf = dst, .len = payload_len};
	bi->burst_tx_set = (struct spi_buf_set){.buffers = bi->burst_tx,
						.count = ARRAY_SIZE(bi->burst_tx)};
	bi->burst_rx_set = (struct spi_buf_set){.buffers = bi->burst_rx,
						.count = ARRAY_SIZE(bi->burst_rx)};
}

int32_t bgt60tr13c_fifo_read_status(const struct bgt60tr13c_iface *bi, int spi_result)
{
	if (spi_result != 0) {
		return XENSIV_BGT60TRXX_STATUS_COM_ERROR;
	}

	/* GSR0 is bits [27:24] of the big-endian status word. */
	uint8_t gsr0 = (uint8_t)((sys_get_be32(bi->burst_status) >> 24) & 0x0FU);

	if (gsr0 & (XENSIV_BGT60TRXX_REG_GSR0_FOU_ERR_MSK |
		    XENSIV_BGT60TRXX_REG_GSR0_SPI_BURST_ERR_MSK |
		    XENSIV_BGT60TRXX_REG_GSR0_CLK_NUM_ERR_MSK)) {
		return XENSIV_BGT60TRXX_STATUS_GSR0_ERROR;
	}

	return XENSIV_BGT60TRXX_STATUS_OK;
}

/* A FIFO burst that waits for the bus, for the vendor library's own FIFO
 * read only; the driver reads with bgt60tr13c_fifo_read_async().
 */
static int32_t bgt60tr13c_fifo_burst(struct bgt60tr13c_iface *bi, uint32_t cmd, uint8_t *dst,
				     size_t payload_len)
{
	bgt60tr13c_fifo_burst_setup(bi, cmd, dst, payload_len);

	return bgt60tr13c_fifo_read_status(
		bi, spi_transceive_dt(&bi->spi, &bi->burst_tx_set, &bi->burst_rx_set));
}

/* The FIFO burst-read command, as xensiv_bgt60trxx_get_fifo_data() builds
 * it. The library keeps these two constants private to its .c file.
 */
#define BGT60TR13C_SPI_BURST_MODE_CMD      0xFF000000UL
#define BGT60TR13C_SPI_BURST_MODE_SADR_POS 17U

int bgt60tr13c_fifo_read_async(struct bgt60tr13c_iface *bi, uint8_t *dst,
			       uint32_t num_samples, spi_callback_t cb, void *userdata)
{
	uint32_t cmd = BGT60TR13C_SPI_BURST_MODE_CMD |
		       (XENSIV_BGT60TRXX_REG_FIFO_TR13C << BGT60TR13C_SPI_BURST_MODE_SADR_POS);
	size_t payload_len = (num_samples / XENSIV_BGT60TRXX_NUM_SAMPLES_FIFO_WORD) *
			     XENSIV_BGT60TRXX_FIFO_WORD_SIZE_BYTES;

	bgt60tr13c_fifo_burst_setup(bi, sys_cpu_to_be32(cmd), dst, payload_len);
	return spi_transceive_cb(bi->spi.bus, &bi->spi.config, &bi->burst_tx_set,
				 &bi->burst_rx_set, cb, userdata);
}


int32_t xensiv_bgt60trxx_platform_spi_fifo_read(void *iface, uint32_t reg_addr,
						 uint16_t *rx_data, uint32_t num_samples)
{
	struct bgt60tr13c_iface *bi = iface;
	uint32_t num_words = num_samples / XENSIV_BGT60TRXX_NUM_SAMPLES_FIFO_WORD;
	size_t packed_len = num_words * XENSIV_BGT60TRXX_FIFO_WORD_SIZE_BYTES;
	/* 4 bytes per word unpacked, 3 packed: the packed bytes start 1 byte per word in. */
	uint8_t *packed = (uint8_t *)rx_data + num_words;
	int32_t ret = bgt60tr13c_fifo_burst(bi, reg_addr, packed, packed_len);

	if (ret != XENSIV_BGT60TRXX_STATUS_OK) {
		return ret;
	}

	for (uint32_t w = 0; w < num_words; w++) {
		uint8_t b0 = packed[3 * w];
		uint8_t b1 = packed[3 * w + 1];
		uint8_t b2 = packed[3 * w + 2];

		rx_data[2 * w] = (uint16_t)(((uint16_t)b0 << 4) | (b1 >> 4));
		rx_data[2 * w + 1] = (uint16_t)((((uint16_t)b1 & 0x0FU) << 8) | b2);
	}

	return XENSIV_BGT60TRXX_STATUS_OK;
}