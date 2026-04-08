/*
 * Copyright 2023 Google LLC
 * Copyright 2026 badjeff
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_INPUT_ANALOG_AXIS_HIRES_SETTINGS_H_
#define ZEPHYR_INCLUDE_INPUT_ANALOG_AXIS_HIRES_SETTINGS_H_

#include <stdint.h>
#include <zephyr/device.h>

/**
 * @addtogroup input_analog_axis_hires
 * @{
 */

/**
 * @brief Save the calibration data.
 *
 * Save the calibration data permanently on the specified device, requires
 * the @ref settings subsystem to be configured and initialized.
 *
 * @param dev Analog axis hi-res device.
 *
 * @retval 0 If successful.
 * @retval -errno In case of any other error.
 */
int analog_axis_hires_calibration_save(const struct device *dev);

/** @} */

#endif /* ZEPHYR_INCLUDE_INPUT_ANALOG_AXIS_HIRES_SETTINGS_H_ */
