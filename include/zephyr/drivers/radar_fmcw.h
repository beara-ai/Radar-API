/*
 * Copyright (c) 2026 Beara.ai
 * SPDX-License-Identifier: Apache-2.0
 */

/**
 * @file
 * @ingroup radar_fmcw_interface
 * @brief Main header file for the raw-frame FMCW radar driver API.
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_RADAR_FMCW_H_
#define ZEPHYR_INCLUDE_DRIVERS_RADAR_FMCW_H_

/**
 * @brief Interfaces for raw-frame FMCW radars.
 * @defgroup radar_fmcw_interface FMCW radar
 * @since 4.5
 * @version 0.1.0
 * @ingroup io_interfaces
 *
 * A driver implementing this API delivers complete raw ADC frames through
 * RTIO, the same mechanism as the sensor API's streaming, and describes
 * them in two parts:
 *  - struct radar_fmcw_device_info, what never changes for a given radar:
 *    its physical antennas, their positions and their calibration,
 *  - struct radar_fmcw_frame_config, what the current chirp/frame profile
 *    sets: which antennas receive and transmit on each chirp, how the
 *    samples are stored, sample and chirp counts, frequencies and timing.
 *
 * Each driver delivers samples in its radar's own format and order, so it
 * never has to unpack or reorder them, and the frame config says which. The
 * format is one of a small set (enum radar_fmcw_sample_format) and the order
 * is given by three strides. Consumers read samples with
 * radar_fmcw_frame_read_chirp(), which handles every format, so they don't
 * depend on the radar either.
 *
 * Only data every FMCW radar shares belongs in these structs, with a
 * neutral value where a radar lacks a feature (one TX antenna, real
 * samples, zero calibration). Settings particular to one chip, such as its
 * IF gain or high-pass filter, belong to that chip's driver.
 * Processing code configures itself from the two, so an application never
 * copies chirp parameters or antenna geometry by hand and the same
 * processing works with any FMCW radar that has a driver.
 *
 * Where the reported values come from is up to the driver; the intended
 * split is:
 *  - fixed properties of the chip (antenna count, ADC format) in the
 *    driver itself,
 *  - board-dependent properties (antenna geometry, mounting orientation)
 *    in the radar's devicetree node,
 *  - the chirp/frame profile chosen for the application, also in the
 *    radar's devicetree node, using the generic properties of
 *    dts/bindings/radar/radar-fmcw.yaml that every radar binding includes.
 *    Each radar has its own profile. A driver reads it with
 *    RADAR_FMCW_DT_PROFILE_DEFINE(), which checks it at build time, and
 *    either programs its radar from it or, if it programs a fixed
 *    configuration such as a vendor register list, checks with the
 *    RADAR_FMCW_DT_CHECK_*() macros that the two match. Either way the build
 *    fails, naming the radar and the property, rather than the radar
 *    delivering frames other than the ones described.
 *
 * @{
 */

#include <errno.h>
#include <stddef.h>
#include <stdint.h>
#include <string.h>

#include <zephyr/device.h>
#include <zephyr/kernel.h>
#include <zephyr/rtio/rtio.h>
#ifdef CONFIG_SENSOR_CLOCK
#include <zephyr/drivers/sensor_clock.h>
#endif

#ifdef __cplusplus
extern "C" {
#endif

/** Most physical RX antennas a radar_fmcw_device_info can describe. */
#define RADAR_FMCW_MAX_RX 4
/** Most physical TX antennas a radar_fmcw_device_info can describe. */
#define RADAR_FMCW_MAX_TX 4
/** Longest repeating TX pattern a radar_fmcw_frame_config can describe. */
#define RADAR_FMCW_MAX_TX_PATTERN 8

/** Index of the azimuth coordinate in an antenna position. */
#define RADAR_FMCW_AZ 0
/** Index of the elevation coordinate in an antenna position. */
#define RADAR_FMCW_EL 1

/**
 * @brief Fixed description of a radar.
 *
 * Everything here stays the same whatever profile the radar runs: it comes
 * from the chip and from how the board fits it.
 *
 * Antenna positions are phase-centre positions as {azimuth, elevation} in
 * micrometres. Viewed from the front of the radar: azimuth positive to the
 * right, elevation positive up. RX and TX positions share one origin, which
 * may be anywhere, and the board's mounting orientation is already applied
 * - this is the geometry as the scene sees it.
 */
struct radar_fmcw_device_info {
	/** Physical RX antennas on the radar. */
	uint8_t num_rx;
	/** Physical TX antennas on the radar. */
	uint8_t num_tx;
	/** Position of each physical RX antenna. */
	int32_t rx_pos_um[RADAR_FMCW_MAX_RX][2];
	/**
	 * Position of each physical TX antenna. Only processing that uses
	 * several TX antennas (MIMO) needs it: with a single TX antenna its
	 * position adds the same phase to every channel, so a driver for a
	 * single-TX radar may report zero.
	 */
	int32_t tx_pos_um[RADAR_FMCW_MAX_TX][2];
	/**
	 * Phase error of each physical RX antenna's receive path, in
	 * millidegrees. Subtract it from a channel's measured phase to
	 * calibrate it. Zero when the radar isn't calibrated.
	 */
	int32_t rx_phase_mdeg[RADAR_FMCW_MAX_RX];
	/**
	 * Gain error of each physical RX antenna's receive path, in
	 * millidecibels. Subtract it from a channel's measured level to
	 * calibrate it. Zero when the radar isn't calibrated.
	 */
	int32_t rx_gain_mdb[RADAR_FMCW_MAX_RX];
};

/** @brief How each sample in a frame is represented. */
enum radar_fmcw_sample_type {
	/** One real ADC value per sample. */
	RADAR_FMCW_SAMPLE_REAL = 0,
	/** One I value then one Q value per sample. */
	RADAR_FMCW_SAMPLE_COMPLEX,
};

/** @brief How each ADC value is stored in a frame. */
enum radar_fmcw_sample_format {
	/** Each value is an int16_t, in CPU byte order. */
	RADAR_FMCW_FORMAT_S16 = 0,
	/**
	 * Unsigned 12-bit values, two packed into every three bytes, most
	 * significant bits first:
	 *
	 *     byte 0: v0[11:4]   byte 1: v0[3:0] v1[11:8]   byte 2: v1[7:0]
	 *
	 * As the Infineon BGT60 family's FIFO stores them. Frames hold an
	 * even number of values.
	 */
	RADAR_FMCW_FORMAT_U12_PACKED,
};

/**
 * @brief Bytes needed to store values in a given format.
 *
 * A constant expression when both arguments are, so it can size buffers at
 * build time.
 *
 * @param format     enum radar_fmcw_sample_format.
 * @param num_values ADC values (two per complex sample).
 */
#define RADAR_FMCW_DATA_SIZE(format, num_values)                                                 \
	(((format) == RADAR_FMCW_FORMAT_U12_PACKED) ? (((size_t)(num_values) * 3U + 1U) / 2U)   \
						     : ((size_t)(num_values) * sizeof(int16_t)))

/**
 * @brief Description of the frames a radar delivers.
 *
 * Integer units throughout, so drivers need no floating point.
 */
struct radar_fmcw_frame_config {
	/** RX channels present in every frame. */
	uint8_t num_rx;
	/**
	 * Physical antenna behind each frame channel, in frame order, as an
	 * index into radar_fmcw_device_info::rx_pos_um. A profile may enable
	 * only some of the antennas.
	 */
	uint8_t rx_index[RADAR_FMCW_MAX_RX];
	/**
	 * Length of the TX pattern below, at least 1. 1 for a radar that
	 * transmits the same way on every chirp.
	 */
	uint8_t tx_pattern_len;
	/**
	 * TX antennas that transmit on each chirp, as a bit mask of physical
	 * TX antennas (bit n = radar_fmcw_device_info::tx_pos_um[n]). Chirp c
	 * uses tx_pattern[c % tx_pattern_len], so time-division MIMO over
	 * three antennas is {0x1, 0x2, 0x4} with a length of 3.
	 */
	uint8_t tx_pattern[RADAR_FMCW_MAX_TX_PATTERN];
	/** Whether samples are real, or complex I/Q pairs. */
	enum radar_fmcw_sample_type sample_type;
	/** How each ADC value is stored in the frame. */
	enum radar_fmcw_sample_format format;
	/**
	 * Distance between frame channels, in samples. With
	 * @ref chirp_stride and @ref sample_stride it gives the order of the
	 * samples in the frame: sample s of chirp c on frame channel rx is
	 * sample number
	 *
	 *     rx * rx_stride + c * chirp_stride + s * sample_stride
	 *
	 * For receivers interleaved sample by sample, as the BGT60 FIFO delivers them:
	 * sample_stride = num_rx, rx_stride = 1 and
	 * chirp_stride = num_rx * samples_per_chirp.
	 */
	uint32_t rx_stride;
	/** Distance between chirps, in samples. See @ref rx_stride. */
	uint32_t chirp_stride;
	/** Distance between samples of one chirp, in samples. See @ref rx_stride. */
	uint32_t sample_stride;
	/** ADC resolution in bits. Every value fits in an int16_t. */
	uint8_t adc_bits;
	/**
	 * Value subtracted from every raw value to centre it on zero,
	 * e.g. 2048 for an unsigned 12-bit ADC, 0 for a signed one.
	 */
	int16_t adc_offset;
	/** ADC samples in each chirp. */
	uint16_t samples_per_chirp;
	/** Chirps in each frame, counting every chirp of the TX pattern. */
	uint16_t chirps_per_frame;
	/** ADC sample rate (Hz). */
	uint32_t sample_rate_hz;
	/**
	 * RF frequency at the first ADC sample of a chirp (Hz). With
	 * @ref stop_freq_hz this gives the sampled bandwidth, which sets the
	 * range resolution (c / 2B).
	 */
	uint64_t start_freq_hz;
	/** RF frequency at the last ADC sample of a chirp (Hz). */
	uint64_t stop_freq_hz;
	/** Chirp-to-chirp repetition time (ns). Sets velocity resolution. */
	uint32_t chirp_period_ns;
	/** Frame-to-frame repetition time (us). */
	uint32_t frame_period_us;
};

/**
 * The sensor clock, sensor_clock_get_cycles(), when CONFIG_SENSOR_CLOCK is
 * enabled, so radar frames share a time base with sensor API data.
 * Otherwise the system cycle counter, which is the sensor clock's default
 * source (CONFIG_SENSOR_CLOCK_SYSTEM). Safe to call from an interrupt
 * handler.
 */
static inline int radar_fmcw_clock_get_cycles(uint64_t *cycles)
{
#if defined(CONFIG_SENSOR_CLOCK)
	return sensor_clock_get_cycles(cycles);
#elif defined(CONFIG_TIMER_HAS_64BIT_CYCLE_COUNTER)
	*cycles = k_cycle_get_64();
	return 0;
#else
	*cycles = (uint64_t)k_cycle_get_32();
	return 0;
#endif
}

static inline uint64_t radar_fmcw_clock_cycles_to_ns(uint64_t cycles)
{
#if defined(CONFIG_SENSOR_CLOCK)
	return sensor_clock_cycles_to_ns(cycles);
#else
	return k_cyc_to_ns_near64(cycles);
#endif
}

/**
 * @brief Header at the start of every frame buffer.
 *
 * Frames are delivered through RTIO into buffers from the application's
 * RTIO memory pool. Each buffer holds this header followed immediately by
 * the sample data, stored as radar_fmcw_frame_config::format and in the
 * order its strides give. Use radar_fmcw_frame_from_buf() rather than
 * reading it directly.
 */
struct radar_fmcw_frame_header {
	/**
	 * When the frame started, as near as the driver can tell (e.g. the
	 * first FIFO interrupt of the frame), in nanoseconds on the
	 * radar_fmcw_clock_get_cycles() clock. Not when the frame completed.
	 */
	uint64_t timestamp_ns;
	/** Driver's running frame counter. Gaps mean dropped frames. */
	uint32_t frame_seen;
	/** Bytes of sample data following the header. */
	uint32_t data_len;
};

BUILD_ASSERT(sizeof(struct radar_fmcw_frame_header) == 16,
	     "radar_fmcw_frame_header is part of the buffer format and must stay 16 bytes");

/** Bytes of RTIO buffer needed for a frame */
#define RADAR_FMCW_FRAME_BUF_SIZE(data_len)                                                     \
	(sizeof(struct radar_fmcw_frame_header) + (size_t)(data_len))

/**
 * The sample data is raw ADC codes (subtract
 * radar_fmcw_frame_config::adc_offset to centre them), stored as the frame
 * config's format and in the order its strides give. Read them with
 * radar_fmcw_frame_read_chirp(). Chirps are in transmit order; see
 * radar_fmcw_frame_config::tx_pattern for which TX antennas sent each.
 */
struct radar_fmcw_frame {
	/** Sample data. Points into the RTIO buffer; valid until that buffer is released. */
	const uint8_t *data;
	/** Bytes of sample data. */
	size_t data_len;
	/** Driver's running frame counter. Gaps mean dropped frames. */
	uint32_t frame_seen;
	/** See radar_fmcw_frame_header::timestamp_ns. */
	uint64_t timestamp_ns;
};

/**
 * Allows for any gaps the strides leave. Size buffers with
 * RADAR_FMCW_FRAME_BUF_SIZE(radar_fmcw_frame_data_size(cfg)).
 *
 * @return Bytes of sample data, or 0 for a frame with no samples or a NULL
 *         @p cfg.
 */
static inline size_t radar_fmcw_frame_data_size(const struct radar_fmcw_frame_config *cfg)
{
	size_t per_sample;
	size_t last;

	if (cfg == NULL || cfg->num_rx == 0 || cfg->chirps_per_frame == 0 ||
	    cfg->samples_per_chirp == 0) {
		return 0;
	}

	per_sample = (cfg->sample_type == RADAR_FMCW_SAMPLE_COMPLEX) ? 2 : 1;
	last = (size_t)(cfg->num_rx - 1) * cfg->rx_stride +
	       (size_t)(cfg->chirps_per_frame - 1) * cfg->chirp_stride +
	       (size_t)(cfg->samples_per_chirp - 1) * cfg->sample_stride;

	return RADAR_FMCW_DATA_SIZE(cfg->format, (last + 1) * per_sample);
}

/**
 * The frame profile a radar's devicetree node describes.
 *
 * A driver defines one for each radar with RADAR_FMCW_DT_PROFILE_DEFINE()
 * and copies it into its frame description
 * with radar_fmcw_frame_config_from_profile(); the rest of the frame
 * description, how the samples are stored, comes from the driver.
 */
struct radar_fmcw_profile {
	uint64_t start_freq_hz;
	uint64_t stop_freq_hz;
	uint32_t sample_rate_hz;
	uint32_t chirp_period_ns;
	uint32_t frame_period_us;
	uint16_t samples_per_chirp;
	uint16_t chirps_per_frame;
	uint8_t rx_mask;
	uint8_t tx_pattern_len;
	uint8_t tx_pattern[RADAR_FMCW_MAX_TX_PATTERN];
};

/**
 * RX channels in the frames of a radar's devicetree profile.
 *
 * A constant expression, so it can size buffers at build time.
 */
#define RADAR_FMCW_DT_NUM_RX(node_id) POPCOUNT(DT_PROP(node_id, rx_mask))

/**
 * Samples in each frame of a radar's devicetree profile, over every
 * channel.
 *
 * Twice as many ADC values for a radar that samples complex I/Q pairs. A
 * constant expression, so it can size buffers at build time.
 */
#define RADAR_FMCW_DT_FRAME_SAMPLES(node_id)                                                     \
	(RADAR_FMCW_DT_NUM_RX(node_id) * DT_PROP(node_id, chirps_per_frame) *                   \
	 DT_PROP(node_id, samples_per_chirp))

/** @cond INTERNAL_HIDDEN */
#define Z_RADAR_FMCW_DT_MSG(node_id, what) "FMCW radar " DT_NODE_PATH(node_id) ": " what

/* Devicetree spelling of each property RADAR_FMCW_DT_CHECK_PROP() takes. */
#define Z_RADAR_FMCW_DT_NAME_start_freq_khz    "start-freq-khz"
#define Z_RADAR_FMCW_DT_NAME_stop_freq_khz     "stop-freq-khz"
#define Z_RADAR_FMCW_DT_NAME_samples_per_chirp "samples-per-chirp"
#define Z_RADAR_FMCW_DT_NAME_chirps_per_frame  "chirps-per-frame"
#define Z_RADAR_FMCW_DT_NAME_sample_rate_hz    "sample-rate-hz"
#define Z_RADAR_FMCW_DT_NAME_chirp_period_ns   "chirp-period-ns"
#define Z_RADAR_FMCW_DT_NAME_frame_period_us   "frame-period-us"
#define Z_RADAR_FMCW_DT_NAME_rx_mask           "rx-mask"

/* Whether tx-pattern entry idx enables some, and only existing, TX antennas;
 * followed by && so an empty pattern still gives an expression.
 */
#define Z_RADAR_FMCW_DT_TX_ENTRY_OK(node_id, prop, idx, num_phys_tx)                             \
	(DT_PROP_BY_IDX(node_id, prop, idx) != 0 &&                                             \
	 (DT_PROP_BY_IDX(node_id, prop, idx) & ~BIT_MASK(num_phys_tx)) == 0) &&

/* Whether tx-pattern entry idx exists and is mask. */
#define Z_RADAR_FMCW_DT_TX_ENTRY_IS(idx, mask, node_id)                                          \
	COND_CODE_1(DT_PROP_HAS_IDX(node_id, tx_pattern, idx),                                  \
		    ((DT_PROP_BY_IDX(node_id, tx_pattern, idx) == (mask))), (0))

#define Z_RADAR_FMCW_DT_PROFILE_VALIDATE(node_id, num_phys_rx, num_phys_tx)                      \
	BUILD_ASSERT((num_phys_rx) >= 1 && (num_phys_rx) <= RADAR_FMCW_MAX_RX &&                \
			     (num_phys_tx) >= 1 && (num_phys_tx) <= RADAR_FMCW_MAX_TX,                 \
		     "RADAR_FMCW_DT_PROFILE_DEFINE(): the radar's antenna counts must be "     \
		     "1 to RADAR_FMCW_MAX_RX/TX");                                              \
	BUILD_ASSERT(DT_PROP(node_id, samples_per_chirp) > 0 &&                                 \
			     DT_PROP(node_id, chirps_per_frame) > 0 &&                                 \
			     DT_PROP(node_id, sample_rate_hz) > 0 &&                                   \
			     DT_PROP(node_id, chirp_period_ns) > 0 &&                                  \
			     DT_PROP(node_id, frame_period_us) > 0,                                    \
		     Z_RADAR_FMCW_DT_MSG(node_id, "samples-per-chirp, chirps-per-frame, "      \
						  "sample-rate-hz, chirp-period-ns and "      \
						  "frame-period-us must be above zero"));     \
	BUILD_ASSERT(DT_PROP(node_id, stop_freq_khz) > DT_PROP(node_id, start_freq_khz),        \
		     Z_RADAR_FMCW_DT_MSG(node_id,                                               \
					 "stop-freq-khz must be above start-freq-khz"));        \
	BUILD_ASSERT((uint64_t)DT_PROP(node_id, samples_per_chirp) * 1000000000ULL <=           \
			     (uint64_t)DT_PROP(node_id, chirp_period_ns) *                             \
				     DT_PROP(node_id, sample_rate_hz),                                 \
		     Z_RADAR_FMCW_DT_MSG(node_id, "samples-per-chirp samples at sample-rate-hz " \
						  "take longer than chirp-period-ns"));       \
	BUILD_ASSERT((uint64_t)DT_PROP(node_id, chirps_per_frame) *                             \
				     DT_PROP(node_id, chirp_period_ns) <=                              \
			     (uint64_t)DT_PROP(node_id, frame_period_us) * 1000ULL,                    \
		     Z_RADAR_FMCW_DT_MSG(node_id, "chirps-per-frame chirps of chirp-period-ns " \
						  "take longer than frame-period-us"));       \
	BUILD_ASSERT(DT_PROP(node_id, rx_mask) != 0 &&                                          \
			     (DT_PROP(node_id, rx_mask) & ~BIT_MASK(num_phys_rx)) == 0,                \
		     Z_RADAR_FMCW_DT_MSG(node_id, "rx-mask must enable at least one RX "       \
						  "antenna, and only antennas the radar has"));\
	BUILD_ASSERT(DT_PROP_LEN(node_id, tx_pattern) >= 1 &&                                   \
			     DT_PROP_LEN(node_id, tx_pattern) <= RADAR_FMCW_MAX_TX_PATTERN,            \
		     Z_RADAR_FMCW_DT_MSG(node_id, "tx-pattern must have 1 to " STRINGIFY(       \
						  RADAR_FMCW_MAX_TX_PATTERN) " entries"));    \
	BUILD_ASSERT(DT_FOREACH_PROP_ELEM_VARGS(node_id, tx_pattern, Z_RADAR_FMCW_DT_TX_ENTRY_OK, \
						num_phys_tx) 1,                                 \
		     Z_RADAR_FMCW_DT_MSG(node_id, "each tx-pattern entry must enable at least " \
						  "one TX antenna, and only antennas the "    \
						  "radar has"));                              \
	BUILD_ASSERT(DT_PROP_LEN(node_id, tx_pattern) == 0 ||                                   \
			     DT_PROP(node_id, chirps_per_frame) % DT_PROP_LEN(node_id, tx_pattern) == 0, \
		     Z_RADAR_FMCW_DT_MSG(node_id, "chirps-per-frame must be a whole number of " \
						  "tx-pattern repeats"))
/** @endcond */

/**
 * Define a radar's frame profile from its devicetree node.
 *
 * Defines a static const struct radar_fmcw_profile holding the radar's
 * radar-fmcw.yaml properties, and checks at build time that they describe
 * frames the radar can produce. The build fails with a message naming the
 * radar and the property.
 *
 * Every driver uses it, once for each of its radars. A driver that programs
 * its radar from the profile needs nothing more. One that programs a fixed
 * configuration, such as a register list from a vendor tool, also checks
 * that configuration against the profile with RADAR_FMCW_DT_CHECK_PROP(),
 * RADAR_FMCW_DT_CHECK_NUM_RX() and RADAR_FMCW_DT_CHECK_TX_PATTERN().
 */
#define RADAR_FMCW_DT_PROFILE_DEFINE(name, node_id, num_phys_rx, num_phys_tx)                    \
	Z_RADAR_FMCW_DT_PROFILE_VALIDATE(node_id, num_phys_rx, num_phys_tx);                     \
	static const struct radar_fmcw_profile name = {                                          \
		.start_freq_hz = (uint64_t)DT_PROP(node_id, start_freq_khz) * 1000ULL,          \
		.stop_freq_hz = (uint64_t)DT_PROP(node_id, stop_freq_khz) * 1000ULL,            \
		.sample_rate_hz = DT_PROP(node_id, sample_rate_hz),                              \
		.chirp_period_ns = DT_PROP(node_id, chirp_period_ns),                            \
		.frame_period_us = DT_PROP(node_id, frame_period_us),                            \
		.samples_per_chirp = DT_PROP(node_id, samples_per_chirp),                        \
		.chirps_per_frame = DT_PROP(node_id, chirps_per_frame),                          \
		.rx_mask = DT_PROP(node_id, rx_mask),                                            \
		.tx_pattern_len = DT_PROP_LEN(node_id, tx_pattern),                              \
		.tx_pattern = DT_PROP(node_id, tx_pattern),                                      \
	}

/**
 * Like RADAR_FMCW_DT_PROFILE_DEFINE(), for a DT_DRV_COMPAT instance.
 */

#define RADAR_FMCW_DT_INST_PROFILE_DEFINE(name, inst, num_phys_rx, num_phys_tx)                  \
	RADAR_FMCW_DT_PROFILE_DEFINE(name, DT_DRV_INST(inst), num_phys_rx, num_phys_tx)

/**
 * Check one devicetree profile property against what a driver
 * programs.
 *
 * For a driver that programs a fixed configuration rather than deriving it
 * from the profile: fails the build, naming the radar and the property,
 * unless the property equals the value that configuration produces.
 */

#define RADAR_FMCW_DT_CHECK_PROP(node_id, prop, value, source)                                   \
	BUILD_ASSERT(DT_PROP(node_id, prop) == (value),                                          \
		     Z_RADAR_FMCW_DT_MSG(node_id, Z_RADAR_FMCW_DT_NAME_##prop " doesn't match " \
						  source))

/**
 * Check how many RX antennas a devicetree profile enables against
 * what a driver programs.
 *
 * For a driver whose fixed configuration only says how many RX antennas it
 * enables, not which; use RADAR_FMCW_DT_CHECK_PROP() on rx_mask when it
 * says which.
 */
#define RADAR_FMCW_DT_CHECK_NUM_RX(node_id, num_rx, source)                                      \
	BUILD_ASSERT(RADAR_FMCW_DT_NUM_RX(node_id) == (num_rx),                                  \
		     Z_RADAR_FMCW_DT_MSG(node_id, "rx-mask doesn't enable as many RX antennas " \
						  "as " source))

/**
 * Check a devicetree profile's TX pattern against what a driver
 * programs.
 *
 * @param ...     The TX masks the driver's configuration uses, chirp by
 *                chirp, e.g. 0x1 for one antenna on every chirp.
 */
#define RADAR_FMCW_DT_CHECK_TX_PATTERN(node_id, source, ...)                                     \
	BUILD_ASSERT(DT_PROP_LEN(node_id, tx_pattern) == NUM_VA_ARGS(__VA_ARGS__) &&            \
			     (FOR_EACH_IDX_FIXED_ARG(Z_RADAR_FMCW_DT_TX_ENTRY_IS, (&&), node_id,       \
						     __VA_ARGS__)),                            \
		     Z_RADAR_FMCW_DT_MSG(node_id, "tx-pattern doesn't match " source))

/**
 * @brief Describe a radar's frames from its frame profile.
 *
 * Fills in the fields of cfg the profile gives (channels, TX pattern,
 * sample and chirp counts, frequencies and timing) and zeroes the rest, for
 * the driver to fill in how its radar stores the samples (sample type,
 * format, strides and ADC). Intended for a driver's get_frame_config.
 *
 * @param cfg     Frame description to fill in.
 * @param profile Profile from RADAR_FMCW_DT_PROFILE_DEFINE().
 */
void radar_fmcw_frame_config_from_profile(struct radar_fmcw_frame_config *cfg,
					  const struct radar_fmcw_profile *profile);



/**
 * @brief Callback API to describe the radar itself.
 * See radar_fmcw_get_device_info() for argument description.
 */
typedef int (*radar_fmcw_api_get_device_info_t)(const struct device *dev,
						 struct radar_fmcw_device_info *info);

/**
 * @brief Callback API to describe the frames a radar delivers.
 * See radar_fmcw_get_frame_config() for argument description.
 */
typedef int (*radar_fmcw_api_get_frame_config_t)(const struct device *dev,
						  struct radar_fmcw_frame_config *cfg);

/**
 * @brief Callback API to queue a frame request with the driver.
 *
 * Called by the RTIO iodev for each read submitted to it. The driver keeps
 * the request until the next frame starts, fills the request's buffer with
 * that frame and completes it. Must not block.
 */
typedef void (*radar_fmcw_api_submit_t)(const struct device *dev,
					struct rtio_iodev_sqe *iodev_sqe);

/**
 * @brief Callback API to start or stop frame generation.
 * See radar_fmcw_start() and radar_fmcw_stop() for argument description.
 */
typedef int (*radar_fmcw_api_start_stop_t)(const struct device *dev);

/**
 * @driver_ops{FMCW radar}
 */
__subsystem struct radar_fmcw_driver_api {
	/** @driver_ops_mandatory @copybrief radar_fmcw_get_device_info */
	radar_fmcw_api_get_device_info_t get_device_info;
	/** @driver_ops_mandatory @copybrief radar_fmcw_get_frame_config */
	radar_fmcw_api_get_frame_config_t get_frame_config;
	/** @driver_ops_mandatory Queue a frame request (see radar_fmcw_api_submit_t) */
	radar_fmcw_api_submit_t submit;
	/** @driver_ops_mandatory @copybrief radar_fmcw_start */
	radar_fmcw_api_start_stop_t start;
	/** @driver_ops_mandatory @copybrief radar_fmcw_stop */
	radar_fmcw_api_start_stop_t stop;
};

/** @} */

/** @cond INTERNAL_HIDDEN */
extern const struct rtio_iodev_api radar_fmcw_iodev_api;
/** @endcond */

/**
 * @brief Define an RTIO iodev that reads frames from a radar.
 *
 * @param name    Name of the iodev, passed to radar_fmcw_stream().
 * @param node_id Devicetree node of the radar.
 */
#define RADAR_FMCW_DT_IODEV(name, node_id)                                                       \
	RTIO_IODEV_DEFINE(name, &radar_fmcw_iodev_api, (void *)DEVICE_DT_GET(node_id))

/**
 * @brief Describe the radar: its physical antennas and their calibration.
 *
 * The result never changes for a given device, so read it once.
 *
 * @param dev  Radar device.
 * @param info Filled in with the description.
 *
 * @retval 0       success
 * @retval -ENOSYS driver doesn't implement it
 * @retval <0      other negative error from the driver
 */
__syscall int radar_fmcw_get_device_info(const struct device *dev,
					 struct radar_fmcw_device_info *info);

static inline int z_impl_radar_fmcw_get_device_info(const struct device *dev,
						    struct radar_fmcw_device_info *info)
{
	const struct radar_fmcw_driver_api *api = DEVICE_API_GET(radar_fmcw, dev);

	if (api->get_device_info == NULL) {
		return -ENOSYS;
	}
	return api->get_device_info(dev, info);
}

/**
 * @brief Describe the frames the radar delivers with its current profile.
 *
 * @param dev Radar device.
 * @param cfg Filled in with the frame description.
 *
 * @retval 0       success
 * @retval -ENOSYS driver doesn't implement it
 * @retval <0      other negative error from the driver
 */
__syscall int radar_fmcw_get_frame_config(const struct device *dev,
					  struct radar_fmcw_frame_config *cfg);

static inline int z_impl_radar_fmcw_get_frame_config(const struct device *dev,
						     struct radar_fmcw_frame_config *cfg)
{
	const struct radar_fmcw_driver_api *api = DEVICE_API_GET(radar_fmcw, dev);

	if (api->get_frame_config == NULL) {
		return -ENOSYS;
	}
	return api->get_frame_config(dev, cfg);
}

/**
 * @brief Start frame generation.
 *
 * Frames fill requests queued with radar_fmcw_stream().
 *
 * @param dev Radar device.
 *
 * @retval 0       success
 * @retval -ENOSYS driver doesn't implement it
 * @retval -EIO    the radar didn't start
 */
__syscall int radar_fmcw_start(const struct device *dev);

static inline int z_impl_radar_fmcw_start(const struct device *dev)
{
	const struct radar_fmcw_driver_api *api = DEVICE_API_GET(radar_fmcw, dev);

	if (api->start == NULL) {
		return -ENOSYS;
	}
	return api->start(dev);
}

/**
 * @brief Stop frame generation.
 *
 * Queued requests are kept for the next radar_fmcw_start(), except
 * cancelled ones, which are released.
 *
 * @param dev Radar device.
 *
 * @retval 0       success
 * @retval -ENOSYS driver doesn't implement it
 * @retval -EIO    the radar didn't stop
 */
__syscall int radar_fmcw_stop(const struct device *dev);

static inline int z_impl_radar_fmcw_stop(const struct device *dev)
{
	const struct radar_fmcw_driver_api *api = DEVICE_API_GET(radar_fmcw, dev);

	if (api->stop == NULL) {
		return -ENOSYS;
	}
	return api->stop(dev);
}

/**
 * @brief Stream frames from a radar into an RTIO context.
 *
 * Submits a multishot read, so each frame produces one completion whose
 * buffer comes from @p ctx's memory pool (define it with
 * RTIO_DEFINE_WITH_MEMPOOL()). For each completion, fetch the buffer with
 * rtio_cqe_get_mempool_buffer(), parse it with radar_fmcw_frame_from_buf()
 * and give it back with rtio_release_buffer() once done.
 *
 * When the pool has no free buffer at the start of a frame, that frame is
 * dropped and frame_seen skips it. A frame lost to a bus or FIFO error is
 * skipped in the same way, so any gap in frame_seen means lost frames. A
 * completion with a negative result ends the stream (its buffer is already
 * released); submit it again to resume.
 * A radar serves one stream at a time. While a stream is open, another
 * submission completes straight away with -EBUSY; an application that
 * needs frames in several places shares them itself.
 *
 * Cancel the stream with rtio_sqe_cancel() on @p handle, before
 * radar_fmcw_stop(): a stopped radar releases cancelled requests at once,
 * a running one at the start of the next frame.
 *
 * @param iodev    Iodev from RADAR_FMCW_DT_IODEV().
 * @param ctx      RTIO context with a memory pool.
 * @param userdata Passed back in each completion.
 * @param handle   Optional, receives the submission for cancelling.
 *
 * @retval 0       success
 * @retval -ENOMEM no free submission in @p ctx
 */
static inline int radar_fmcw_stream(const struct rtio_iodev *iodev, struct rtio *ctx,
				    void *userdata, struct rtio_sqe **handle)
{
	if (IS_ENABLED(CONFIG_USERSPACE)) {
		struct rtio_sqe sqe;
		int ret;

		rtio_sqe_prep_read_multishot(&sqe, iodev, RTIO_PRIO_NORM, userdata);
		ret = rtio_sqe_copy_in_get_handles(ctx, &sqe, handle, 1);
		if (ret != 0) {
			return ret;
		}
	} else {
		struct rtio_sqe *sqe = rtio_sqe_acquire(ctx);

		if (sqe == NULL) {
			return -ENOMEM;
		}
		if (handle != NULL) {
			*handle = sqe;
		}
		rtio_sqe_prep_read_multishot(sqe, iodev, RTIO_PRIO_NORM, userdata);
	}
	rtio_submit(ctx, 0);
	return 0;
}

/**
 * @brief Parse a completed frame buffer.
 *
 * @param buf     Buffer from rtio_cqe_get_mempool_buffer().
 * @param buf_len Its length.
 * @param frame   Filled in; frame->data points into @p buf.
 *
 * @retval 0       success
 * @retval -EINVAL @p buf or @p frame is NULL, or @p buf is too short for
 *                 the header or the data it lists
 */
static inline int radar_fmcw_frame_from_buf(const uint8_t *buf, uint32_t buf_len,
					    struct radar_fmcw_frame *frame)
{
	struct radar_fmcw_frame_header hdr;

	if (buf == NULL || frame == NULL || buf_len < sizeof(hdr)) {
		return -EINVAL;
	}
	memcpy(&hdr, buf, sizeof(hdr));
	if (hdr.data_len > buf_len - sizeof(hdr)) {
		return -EINVAL;
	}

	frame->data = buf + sizeof(hdr);
	frame->data_len = hdr.data_len;
	frame->frame_seen = hdr.frame_seen;
	frame->timestamp_ns = hdr.timestamp_ns;
	return 0;
}

/**
 * Read one chirp of one channel from a frame, whatever its format.
 *
 * Writes the chirp's samples to out in order, as raw ADC codes widened
 * to int16_t: radar_fmcw_frame_config::samples_per_chirp values for real
 * samples, or twice that for complex ones (I then Q for each sample). Subtract
 * radar_fmcw_frame_config::adc_offset to centre them.
 *
 * Reading a chirp at a time lets a consumer unpack straight into its own
 * working buffer, so a frame never needs a second, unpacked copy.
 *
 * @retval 0        success
 * @retval -EINVAL  rx or chirp out of range, or the frame is too short
 *                  for cfg
 * @retval -ENOTSUP unknown format
 */

int radar_fmcw_frame_read_chirp(const struct radar_fmcw_frame_config *cfg,
				const struct radar_fmcw_frame *frame, uint8_t rx, uint16_t chirp,
				int16_t *out);

#ifdef __cplusplus
}
#endif

/** @} */

#include <zephyr/syscalls/radar_fmcw.h>

#endif /* ZEPHYR_INCLUDE_DRIVERS_RADAR_FMCW_H_ */
