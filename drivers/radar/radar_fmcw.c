/*
 * Copyright (c) 2026 Beara.ai
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/radar_fmcw.h>

/* Routes each read submitted to a RADAR_FMCW_DT_IODEV() to its radar's
 * driver, which completes it when a frame has filled the request's buffer.
 */
static void radar_fmcw_iodev_submit(struct rtio_iodev_sqe *iodev_sqe)
{
	const struct device *dev = iodev_sqe->sqe.iodev->data;
	const struct radar_fmcw_driver_api *api = dev->api;

	if (api == NULL || api->submit == NULL) {
		rtio_iodev_sqe_err(iodev_sqe, -ENOSYS);
		return;
	}
	api->submit(dev, iodev_sqe);
}

const struct rtio_iodev_api radar_fmcw_iodev_api = {
	.submit = radar_fmcw_iodev_submit,
};

void radar_fmcw_frame_config_from_profile(struct radar_fmcw_frame_config *cfg,
					  const struct radar_fmcw_profile *profile)
{
	memset(cfg, 0, sizeof(*cfg));

	/* One channel per enabled antenna, in ascending antenna order. */
	for (uint8_t phys = 0; phys < RADAR_FMCW_MAX_RX; phys++) {
		if ((profile->rx_mask & BIT(phys)) != 0U) {
			cfg->rx_index[cfg->num_rx++] = phys;
		}
	}

	cfg->tx_pattern_len = profile->tx_pattern_len;
	memcpy(cfg->tx_pattern, profile->tx_pattern, sizeof(cfg->tx_pattern));
	cfg->samples_per_chirp = profile->samples_per_chirp;
	cfg->chirps_per_frame = profile->chirps_per_frame;
	cfg->sample_rate_hz = profile->sample_rate_hz;
	cfg->start_freq_hz = profile->start_freq_hz;
	cfg->stop_freq_hz = profile->stop_freq_hz;
	cfg->chirp_period_ns = profile->chirp_period_ns;
	cfg->frame_period_us = profile->frame_period_us;
}

/* Value number i of RADAR_FMCW_FORMAT_U12_PACKED data. */
static int16_t radar_fmcw_u12_get(const uint8_t *data, size_t i)
{
	const uint8_t *p = &data[(i / 2U) * 3U];

	if ((i % 2U) == 0U) {
		return (int16_t)(((uint16_t)p[0] << 4) | (p[1] >> 4));
	}
	return (int16_t)((((uint16_t)p[1] & 0x0FU) << 8) | p[2]);
}

/* Value number i of RADAR_FMCW_FORMAT_S16 data. */
static int16_t radar_fmcw_s16_get(const uint8_t *data, size_t i)
{
	int16_t v;

	memcpy(&v, &data[i * sizeof(int16_t)], sizeof(v));
	return v;
}

/* Copy one chirp's values. Always inlined with a constant `packed`, so each
 * format gets its own loop with the value decode inlined.
 */
static void radar_fmcw_copy_chirp(const struct radar_fmcw_frame_config *cfg,
						const uint8_t *data, size_t first,
						size_t per_sample, bool packed, int16_t *out)
{
	for (uint16_t s = 0; s < cfg->samples_per_chirp; s++) {
		size_t value = (first + (size_t)s * cfg->sample_stride) * per_sample;

		for (size_t v = 0; v < per_sample; v++) {
			*out++ = packed ? radar_fmcw_u12_get(data, value + v)
					: radar_fmcw_s16_get(data, value + v);
		}
	}
}

int radar_fmcw_frame_read_chirp(const struct radar_fmcw_frame_config *cfg,
				const struct radar_fmcw_frame *frame, uint8_t rx, uint16_t chirp,
				int16_t *out)
{
	size_t per_sample;
	size_t first;
	size_t last;

	if (!cfg || !frame || !frame->data || !out) {
		return -EINVAL;
	}

	per_sample = (cfg->sample_type == RADAR_FMCW_SAMPLE_COMPLEX) ? 2 : 1;

	if (cfg->format != RADAR_FMCW_FORMAT_S16 && cfg->format != RADAR_FMCW_FORMAT_U12_PACKED) {
		return -ENOTSUP;
	}
	if (rx >= cfg->num_rx || chirp >= cfg->chirps_per_frame || cfg->samples_per_chirp == 0) {
		return -EINVAL;
	}

	/* Check once that the chirp's last value is inside the frame. */
	first = (size_t)rx * cfg->rx_stride + (size_t)chirp * cfg->chirp_stride;
	last = first + (size_t)(cfg->samples_per_chirp - 1) * cfg->sample_stride;
	if (RADAR_FMCW_DATA_SIZE(cfg->format, (last + 1) * per_sample) > frame->data_len) {
		return -EINVAL;
	}

	bool packed = (cfg->format == RADAR_FMCW_FORMAT_U12_PACKED);
	radar_fmcw_copy_chirp(cfg, frame->data, first, per_sample, packed, out);

	return 0;
}
