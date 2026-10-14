/*
 * Copyright (c) 2026 Beara.ai
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Compiles one bgt60-configurator-cli profile header into the driver under
 * its own names, so several profiles can sit side by side and each radar
 * can use a different one. Deliberately has no include guard: include it
 * once per profile, from bgt60tr13c_profiles.h only, with
 *
 *   BGT60TR13C_PROFILE_ID      the profile's name as a C token, matching its
 *                              devicetree register-profile value with '-'
 *                              written as '_', e.g. people_tracking
 *   BGT60TR13C_PROFILE_HEADER  the generated header, as a string
 *
 * The generated header is included unmodified. Its register_list array is
 * renamed, its values are captured as enum constants (so they stay usable
 * in build-time checks), and then its macros and include guard are
 * undefined so the next profile header can be included.
 */

#define register_list BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, REGS)
#define XENSIV_BGT60TRXX_CONF_IMPL
#include BGT60TR13C_PROFILE_HEADER
#undef XENSIV_BGT60TRXX_CONF_IMPL
#undef register_list

BUILD_ASSERT(XENSIV_BGT60TRXX_CONF_DEVICE == XENSIV_DEVICE_BGT60TR13C,
	     "BGT60TR13C profile " STRINGIFY(BGT60TR13C_PROFILE_ID)
	     " was generated for a different BGT60 device");
BUILD_ASSERT((XENSIV_BGT60TRXX_CONF_START_FREQ_HZ % 1000) == 0 &&
		     (XENSIV_BGT60TRXX_CONF_END_FREQ_HZ % 1000) == 0,
	     "BGT60TR13C profile " STRINGIFY(BGT60TR13C_PROFILE_ID)
	     " frequencies must be whole kHz to be described in devicetree");
/* The driver reports, and checks tx-pattern against, Tx1 on every chirp. */
BUILD_ASSERT(XENSIV_BGT60TRXX_CONF_NUM_TX_ANTENNAS == 1,
	     "BGT60TR13C profile " STRINGIFY(BGT60TR13C_PROFILE_ID)
	     " must transmit on the BGT60TR13C's one TX antenna");

enum {
	BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, START_FREQ_KHZ) =
		XENSIV_BGT60TRXX_CONF_START_FREQ_HZ / 1000,
	BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, STOP_FREQ_KHZ) =
		XENSIV_BGT60TRXX_CONF_END_FREQ_HZ / 1000,
	BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, SAMPLES_PER_CHIRP) =
		XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP,
	BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, CHIRPS_PER_FRAME) =
		XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME,
	BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, SAMPLE_RATE_HZ) =
		XENSIV_BGT60TRXX_CONF_SAMPLE_RATE,
	BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, CHIRP_PERIOD_NS) =
		(int)(XENSIV_BGT60TRXX_CONF_CHIRP_REPETITION_TIME_S * 1e9 + 0.5),
	BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, FRAME_PERIOD_US) =
		(int)(XENSIV_BGT60TRXX_CONF_FRAME_REPETITION_TIME_S * 1e6 + 0.5),
	BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, NUM_RX) =
		XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS,
};

static const struct bgt60tr13c_profile BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, PROFILE)
	__unused = {
	.name = STRINGIFY(BGT60TR13C_PROFILE_ID),
	.regs = BGT60TR13C_PROFILE_SYM(BGT60TR13C_PROFILE_ID, REGS),
	.num_regs = XENSIV_BGT60TRXX_CONF_NUM_REGS,
};

#undef XENSIV_BGT60TRXX_CONF_H
#undef XENSIV_BGT60TRXX_CONF_DEVICE
#undef XENSIV_BGT60TRXX_CONF_START_FREQ_HZ
#undef XENSIV_BGT60TRXX_CONF_END_FREQ_HZ
#undef XENSIV_BGT60TRXX_CONF_NUM_SAMPLES_PER_CHIRP
#undef XENSIV_BGT60TRXX_CONF_NUM_CHIRPS_PER_FRAME
#undef XENSIV_BGT60TRXX_CONF_NUM_RX_ANTENNAS
#undef XENSIV_BGT60TRXX_CONF_NUM_TX_ANTENNAS
#undef XENSIV_BGT60TRXX_CONF_SAMPLE_RATE
#undef XENSIV_BGT60TRXX_CONF_CHIRP_REPETITION_TIME_S
#undef XENSIV_BGT60TRXX_CONF_FRAME_REPETITION_TIME_S
#undef XENSIV_BGT60TRXX_CONF_NUM_REGS
#undef BGT60TR13C_PROFILE_ID
#undef BGT60TR13C_PROFILE_HEADER
