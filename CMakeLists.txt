# Copyright (c) 2026 Beara.ai
# SPDX-License-Identifier: Apache-2.0

zephyr_include_directories(include)
zephyr_syscall_include_directories(include)

add_subdirectory_ifdef(CONFIG_RADAR_FMCW drivers/radar)
