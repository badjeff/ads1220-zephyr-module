/*
 * Copyright (c) 2026 badjeff
 *
 * SPDX-License-Identifier: Apache-2.0
 */
#ifndef ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_ADS1220_GPIO_H_
#define ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_ADS1220_GPIO_H_

/**
 * @brief GPIO pin.
 *
 * The pin flag is a Zephyr specific extension of the standard GPIO flags
 * specified by the Linux GPIO binding. Only applicable for ADS1220.
 */
#define ADS1220_GPIO_PIN_EN   0
#define ADS1220_GPIO_PIN_IDAC 1

#endif /* ZEPHYR_INCLUDE_DT_BINDINGS_GPIO_ADS1220_GPIO_H_ */
