/*
 * Copyright 2023 Google LLC
 * Copyright 2026 badjeff
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <stdlib.h>
#include <zephyr/device.h>
#include <zephyr/logging/log.h>
#include <zephyr/settings/settings.h>
#include <zephyr/sys/printk.h>

LOG_MODULE_REGISTER(analog_axis_hires_settings, CONFIG_INPUT_LOG_LEVEL);

// Headers from this module
#include <zephyr/input/input_analog_axis_hires.h>
#include <zephyr/input/input_analog_axis_hires_settings.h>

#define ANALOG_AXIS_HIRES_SETTINGS_PATH_MAX 32

#define MAX_AXES CONFIG_INPUT_ANALOG_AXIS_HIRES_SETTINGS_MAX_AXES

static void analog_axis_hires_calibration_log(const struct device *dev)
{
	struct analog_axis_hires_calibration cal;
	int i;

	for (i = 0; i < analog_axis_hires_num_axes(dev); i++) {
		analog_axis_hires_calibration_get(dev, i, &cal);

		LOG_INF("%s: ch: %d min: %d max: %d deadzone: %d",
			dev->name, i, cal.in_min, cal.in_max, cal.in_deadzone);
	}
}

static int analog_axis_hires_calibration_load(const char *key, size_t len_rd,
					settings_read_cb read_cb, void *cb_arg)
{
	const struct device *dev;
	struct analog_axis_hires_calibration cal[MAX_AXES];
	int axes;
	char dev_name[ANALOG_AXIS_HIRES_SETTINGS_PATH_MAX];
	const char *next;
	int nlen;
	ssize_t len;

	nlen = settings_name_next(key, &next);
	if (nlen + 1 > sizeof(dev_name)) {
		LOG_ERR("Setting name too long: %d", nlen);
		return -EINVAL;
	}

	memcpy(dev_name, key, nlen);
	dev_name[nlen] = '\0';

	dev = device_get_binding(dev_name);
	if (dev == NULL) {
		LOG_ERR("Cannot restore: device %s not available", dev_name);
		return -ENODEV;
	}

	len = read_cb(cb_arg, cal, sizeof(cal));
	if (len < 0) {
		LOG_ERR("Data restore error: %d", len);
	}

	axes = analog_axis_hires_num_axes(dev);
	if (len != sizeof(struct analog_axis_hires_calibration) * axes) {
		LOG_ERR("Invalid settings data length: %d, expected %d",
			len, sizeof(struct analog_axis_hires_calibration) * axes);
		return -EIO;
	}

	for (int i = 0; i < axes; i++) {
		analog_axis_hires_calibration_set(dev, i, &cal[i]);
	}

	analog_axis_hires_calibration_log(dev);

	return 0;
}

SETTINGS_STATIC_HANDLER_DEFINE(analog_axis_hires, "aa-cal-hires", NULL,
			       analog_axis_hires_calibration_load, NULL, NULL);

int analog_axis_hires_calibration_save(const struct device *dev)
{
	struct analog_axis_hires_calibration cal[MAX_AXES];
	int axes;
	char path[ANALOG_AXIS_HIRES_SETTINGS_PATH_MAX];
	int ret;

	analog_axis_hires_calibration_log(dev);

	ret = snprintk(path, sizeof(path), "aa-cal-hires/%s", dev->name);
	if (ret < 0) {
		return -EINVAL;
	}

	axes = analog_axis_hires_num_axes(dev);
	for (int i = 0; i < axes; i++) {
		analog_axis_hires_calibration_get(dev, i, &cal[i]);
	}

	ret = settings_save_one(path, &cal[0],
				sizeof(struct analog_axis_hires_calibration) * axes);
	if (ret < 0) {
		LOG_ERR("Settings save error: %d", ret);
		return ret;
	}

	return 0;
}
