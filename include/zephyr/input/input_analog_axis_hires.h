/*
 * Copyright 2023 Google LLC
 * Copyright 2026 badjeff
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_INPUT_ANALOG_AXIS_HIRES_H_
#define ZEPHYR_INCLUDE_INPUT_ANALOG_AXIS_HIRES_H_

#include <stdint.h>
#include <zephyr/device.h>

/**
 * @brief Analog axis hi-res API
 * @defgroup input_analog_axis_hires Analog axis hi-res API
 * @ingroup io_interfaces
 * @{
 */

/**
 * @brief Analog axis hi-res calibration data structure.
 *
 * Holds the calibration data for a single analog axis. Initial values are set
 * from the devicetree and can be changed by the application in runtime using
 * @ref analog_axis_hires_calibration_set and @ref analog_axis_hires_calibration_get.
 */
struct analog_axis_hires_calibration {
	/** Input value that corresponds to the minimum output value. */
	int32_t in_min;
	/** Input value that corresponds to the maximum output value. */
	int32_t in_max;
	/** Input value center deadzone. */
	uint32_t in_deadzone;
};

/**
 * @brief Analog axis hi-res calibration state.
 *
 * Holds the runtime calibration state for auto-calibration.
 */
struct analog_axis_hires_calib_state {
	/** Whether auto-calibration has completed. */
	bool calibrated;
	/** Counter for calibration cycles. */
	uint32_t calib_cnt;
	/** Minimum value seen during calibration. */
	int32_t min;
	/** Maximum value seen during calibration. */
	int32_t max;
	/** History buffer for weighted average. */
	int32_t *history;
	/** Current index in history buffer. */
	uint8_t history_idx;
	/** Number of filled history slots. */
	uint8_t history_filled;
};

/**
 * @brief Analog axis hi-res raw data callback.
 *
 * @param dev Analog axis hi-res device.
 * @param channel Channel number.
 * @param raw_val Raw value for the channel (24-bit ADC).
 */
typedef void (*analog_axis_hires_raw_data_t)(const struct device *dev,
				       int channel, int32_t raw_val);

/**
 * @brief Set a raw data callback.
 *
 * Set a callback to receive raw data for the specified analog axis hi-res device.
 * This is meant to be used in the application to acquire the data for calibration.
 * Set cb to NULL to disable the callback.
 *
 * @param dev Analog axis hi-res device.
 * @param cb An analog_axis_hires_raw_data_t callback to use, NULL to disable.
 */
void analog_axis_hires_set_raw_data_cb(const struct device *dev, analog_axis_hires_raw_data_t cb);

/**
 * @brief Get the number of defined axes.
 *
 * @param dev Analog axis hi-res device.
 * @retval n The number of defined axes for dev.
 */
int analog_axis_hires_num_axes(const struct device *dev);

/**
 * @brief Get the axis calibration data.
 *
 * @param dev Analog axis hi-res device.
 * @param channel Channel number.
 * @param cal Pointer to an analog_axis_hires_calibration structure that is going to
 * get set with the current calibration data.
 *
 * @retval 0 If successful.
 * @retval -EINVAL If the specified channel is not valid.
 */
int analog_axis_hires_calibration_get(const struct device *dev,
				int channel,
				struct analog_axis_hires_calibration *cal);

/**
 * @brief Set the axis calibration data.
 *
 * @param dev Analog axis hi-res device.
 * @param channel Channel number.
 * @param cal Pointer to an analog_axis_hires_calibration structure with the new
 * calibration data.
 *
 * @retval 0 If successful.
 * @retval -EINVAL If the specified channel is not valid.
 */
int analog_axis_hires_calibration_set(const struct device *dev,
				int channel,
				struct analog_axis_hires_calibration *cal);

/**
 * @brief Suspend analog axis hi-res sampling.
 *
 * @param dev Analog axis hi-res device.
 */
void analog_axis_hires_suspend(const struct device *dev);

/**
 * @brief Resume analog axis hi-res sampling.
 *
 * @param dev Analog axis hi-res device.
 */
void analog_axis_hires_resume(const struct device *dev);

/** @} */

// Runtime configurable attributes
enum analog_axis_hires_attributes {
  ANALOG_AXIS_HIRES_ATTR_RESUME
};

#endif /* ZEPHYR_INCLUDE_INPUT_ANALOG_AXIS_HIRES_H_ */
