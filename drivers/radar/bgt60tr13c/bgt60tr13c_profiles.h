/*
 * Copyright (c) 2026 Beara.ai
 * SPDX-License-Identifier: Apache-2.0
 */

/*
 * Every register profile compiled into the driver. Each radar picks one
 * with its devicetree register-profile property; the profiles no radar
 * uses are dropped by the linker.
 *
 * To add a built-in profile: put the bgt60-configurator-cli header in
 * profiles/, add it below, and add its name to the register-profile enum
 * in dts/bindings/radar/infineon,bgt60tr13c.yaml.
 */

#ifndef BGT60TR13C_PROFILES_H_
#define BGT60TR13C_PROFILES_H_

#include <stdint.h>
#include <zephyr/sys/util.h>
#include <zephyr/toolchain.h>
#include "xensiv_bgt60trxx.h"

/** A register list that programs the radar, from bgt60-configurator-cli. */
struct bgt60tr13c_profile {
	const char *name;
	const uint32_t *regs;
	uint16_t num_regs;
};

#define BGT60TR13C_CAT3_(a, b, c) a##b##c
#define BGT60TR13C_CAT3(a, b, c)  BGT60TR13C_CAT3_(a, b, c)

/* Name of one value captured from a profile, e.g.
 * BGT60TR13C_PROFILE_SYM(people_tracking, SAMPLES_PER_CHIRP).
 */
#define BGT60TR13C_PROFILE_SYM(id, field) BGT60TR13C_CAT3(bgt60tr13c_profile_, id, _##field)

#define BGT60TR13C_PROFILE_ID     people_tracking
#define BGT60TR13C_PROFILE_HEADER "profiles/bgt60tr13c_people_tracking.h"
#include "bgt60tr13c_profile_wrap.h"

#if defined(CONFIG_BGT60TR13C_PROFILE_CUSTOM)
#define BGT60TR13C_PROFILE_ID     custom
#define BGT60TR13C_PROFILE_HEADER CONFIG_BGT60TR13C_PROFILE_CUSTOM_FILE
#include "bgt60tr13c_profile_wrap.h"
#endif

#endif /* BGT60TR13C_PROFILES_H_ */
