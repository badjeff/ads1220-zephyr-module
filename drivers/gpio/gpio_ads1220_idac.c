/*
 * Copyright (c) 2026 badjeff
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_ads1220_idac

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <stddef.h>
#include <zephyr/drivers/gpio/gpio_utils.h>

LOG_MODULE_REGISTER(gpio_ads1220_idac, CONFIG_GPIO_LOG_LEVEL);

#if CONFIG_ADC_ADS1220
extern int (*_ptr_ads1220_set_idac_ua_by_reg)(uint16_t, uint16_t, bool);
#endif

struct gpio_ads1220_idac_config {
	struct gpio_driver_config common;
	uint16_t dev_reg;
	uint16_t idac_ua_high;
	uint16_t idac_ua_low;
	bool skip_reg_write_high;
	bool skip_reg_write_low;
};

struct gpio_ads1220_idac_data {
	struct gpio_driver_data data;
};

static int gpio_ads1220_idac_pin_configure(const struct device *dev,
					   gpio_pin_t pin, gpio_flags_t flags)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(pin);

	if ((flags & (GPIO_INPUT | GPIO_OUTPUT)) == 0) {
		return 0;
	}
	if ((flags & GPIO_OUTPUT) == 0) {
		return -ENOTSUP;
	}

	return 0;
}

static int gpio_ads1220_idac_port_set_bits_raw(const struct device *dev,
						gpio_port_pins_t mask)
{
	const struct gpio_ads1220_idac_config *cfg = dev->config;
	struct gpio_ads1220_idac_data *data = dev->data;

	if (mask != 0) {
#if CONFIG_ADC_ADS1220
	_ptr_ads1220_set_idac_ua_by_reg(cfg->dev_reg, cfg->idac_ua_high,
		!cfg->skip_reg_write_high);
#endif
	}

	return 0;
}

static int gpio_ads1220_idac_port_clear_bits_raw(const struct device *dev,
						  gpio_port_pins_t mask)
{
	const struct gpio_ads1220_idac_config *cfg = dev->config;
	struct gpio_ads1220_idac_data *data = dev->data;

	if (mask != 0) {
#if CONFIG_ADC_ADS1220
	_ptr_ads1220_set_idac_ua_by_reg(cfg->dev_reg, cfg->idac_ua_low,
		!cfg->skip_reg_write_low);
#endif
	}

	return 0;
}

static const struct gpio_driver_api gpio_ads1220_idac_driver_api = {
	.pin_configure = gpio_ads1220_idac_pin_configure,
	.port_set_bits_raw = gpio_ads1220_idac_port_set_bits_raw,
	.port_clear_bits_raw = gpio_ads1220_idac_port_clear_bits_raw,
};

static int gpio_ads1220_idac_init(const struct device *dev)
{
	LOG_INF("ADS1220 IDAC GPIO initialized");
	return 0;
}

#define GPIO_ADS1220_IDAC_INST_DEFINE(n) \
	static const struct gpio_ads1220_idac_config gpio_ads1220_idac_config_##n = { \
		.common = GPIO_DT_INST_PORT_PIN_MASK_NGPIOS_EXC(n, 32), \
		.dev_reg = DT_INST_PROP_OR(n, dev_reg, 0), \
		.idac_ua_high = DT_INST_PROP_OR(n, idac_ua_high, 0), \
		.idac_ua_low = DT_INST_PROP_OR(n, idac_ua_low, 0), \
		.skip_reg_write_high = DT_INST_PROP_OR(n, skip_reg_write_high, false), \
		.skip_reg_write_low = DT_INST_PROP_OR(n, skip_reg_write_low, false), \
	}; \
	static struct gpio_ads1220_idac_data gpio_ads1220_idac_data_##n = { \
		.data = {0}, \
	}; \
	DEVICE_DT_INST_DEFINE(n, gpio_ads1220_idac_init,	NULL,	\
			&gpio_ads1220_idac_data_##n, &gpio_ads1220_idac_config_##n,	\
			POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, \
			&gpio_ads1220_idac_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_ADS1220_IDAC_INST_DEFINE)
