# Copyright (c) 2026 Beara.ai
# SPDX-License-Identifier: Apache-2.0

zephyr_library()

zephyr_library_sources(radar_fmcw.c)
zephyr_library_sources_ifdef(CONFIG_USERSPACE radar_fmcw_handlers.c)

add_subdirectory_ifdef(CONFIG_BGT60TR13C bgt60tr13c)
