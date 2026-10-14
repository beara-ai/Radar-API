/*
 * Copyright (c) 2026 Beara.ai
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/drivers/radar_fmcw.h>
#include <zephyr/internal/syscall_handler.h>

static inline int z_vrfy_radar_fmcw_get_device_info(const struct device *dev,
						    struct radar_fmcw_device_info *info)
{
	K_OOPS(K_SYSCALL_DRIVER_RADAR_FMCW(dev, get_device_info));
	K_OOPS(K_SYSCALL_MEMORY_WRITE(info, sizeof(struct radar_fmcw_device_info)));
	return z_impl_radar_fmcw_get_device_info(dev, info);
}
#include <zephyr/syscalls/radar_fmcw_get_device_info_mrsh.c>

static inline int z_vrfy_radar_fmcw_get_frame_config(const struct device *dev,
						     struct radar_fmcw_frame_config *cfg)
{
	K_OOPS(K_SYSCALL_DRIVER_RADAR_FMCW(dev, get_frame_config));
	K_OOPS(K_SYSCALL_MEMORY_WRITE(cfg, sizeof(struct radar_fmcw_frame_config)));
	return z_impl_radar_fmcw_get_frame_config(dev, cfg);
}
#include <zephyr/syscalls/radar_fmcw_get_frame_config_mrsh.c>

static inline int z_vrfy_radar_fmcw_start(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_RADAR_FMCW(dev, start));
	return z_impl_radar_fmcw_start(dev);
}
#include <zephyr/syscalls/radar_fmcw_start_mrsh.c>

static inline int z_vrfy_radar_fmcw_stop(const struct device *dev)
{
	K_OOPS(K_SYSCALL_DRIVER_RADAR_FMCW(dev, stop));
	return z_impl_radar_fmcw_stop(dev);
}
#include <zephyr/syscalls/radar_fmcw_stop_mrsh.c>
