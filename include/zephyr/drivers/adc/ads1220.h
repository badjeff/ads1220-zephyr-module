/*
 * Copyright (c) 2023 SILA Embedded Solutions GmbH
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#ifndef ZEPHYR_INCLUDE_DRIVERS_ADC_ADS1220_H_
#define ZEPHYR_INCLUDE_DRIVERS_ADC_ADS1220_H_

#include <zephyr/device.h>
#include <zephyr/drivers/gpio.h>

int ads1220_set_idac_ua_by_reg(uint16_t reg, uint16_t ua, bool write);

int ads1220_device_resume();

int ads1220_device_suspend();

#endif /* ZEPHYR_INCLUDE_DRIVERS_ADC_ADS1220_H_ */
