/*
 * Copyright (c) 2026 badjeff
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT ti_ads1220_gpio

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/sys/util.h>
#include <stddef.h>
#include <zephyr/drivers/gpio/gpio_utils.h>

LOG_MODULE_REGISTER(gpio_ads1220, CONFIG_GPIO_LOG_LEVEL);

#if CONFIG_ADC_ADS1220
#include <zephyr/dt-bindings/gpio/gpio_ads1220.h>
#include <zephyr/drivers/adc/ads1220.h>
#endif

struct gpio_ads1220_config {
	struct gpio_driver_config common;
	uint16_t dev_reg;
	uint16_t idac_ua_high;
	uint16_t idac_ua_low;
	bool skip_reg_write_high;
	bool skip_reg_write_low;
};

struct gpio_ads1220_data {
	struct gpio_driver_data data;
};

static int gpio_ads1220_pin_configure(const struct device *dev,
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

static int gpio_ads1220_port_set_bits_raw(const struct device *dev,
						gpio_port_pins_t mask)
{
	const struct gpio_ads1220_config *cfg = dev->config;
	struct gpio_ads1220_data *data = dev->data;

#if CONFIG_ADC_ADS1220
	if (mask & BIT(ADS1220_GPIO_PIN_IDAC)) {
		ads1220_set_idac_ua_by_reg(cfg->dev_reg, cfg->idac_ua_high,
			!cfg->skip_reg_write_high);
	}
	else if (mask & BIT(ADS1220_GPIO_PIN_EN)) {
		ads1220_device_resume_by_reg(cfg->dev_reg);
	}
#endif

	return 0;
}

static int gpio_ads1220_port_clear_bits_raw(const struct device *dev,
						  gpio_port_pins_t mask)
{
	const struct gpio_ads1220_config *cfg = dev->config;
	struct gpio_ads1220_data *data = dev->data;

#if CONFIG_ADC_ADS1220
	if (mask & BIT(ADS1220_GPIO_PIN_IDAC)) {
		ads1220_set_idac_ua_by_reg(cfg->dev_reg, cfg->idac_ua_low,
			!cfg->skip_reg_write_low);
	}
	else if (mask & BIT(ADS1220_GPIO_PIN_EN)) {
		ads1220_device_suspend_by_reg(cfg->dev_reg);
	}
#endif

	return 0;
}

static const struct gpio_driver_api gpio_ads1220_driver_api = {
	.pin_configure = gpio_ads1220_pin_configure,
	.port_set_bits_raw = gpio_ads1220_port_set_bits_raw,
	.port_clear_bits_raw = gpio_ads1220_port_clear_bits_raw,
};

static int gpio_ads1220_init(const struct device *dev)
{
	ARG_UNUSED(dev);
	LOG_INF("ADS1220 GPIO initialized");
	return 0;
}

#define GPIO_ADS1220_INST_DEFINE(n) \
	static const struct gpio_ads1220_config gpio_ads1220_config_##n = { \
		.common = GPIO_DT_INST_PORT_PIN_MASK_NGPIOS_EXC(n, 32), \
		.dev_reg = DT_INST_PROP_OR(n, dev_reg, 0), \
		.idac_ua_high = DT_INST_PROP_OR(n, idac_ua_high, 0), \
		.idac_ua_low = DT_INST_PROP_OR(n, idac_ua_low, 0), \
		.skip_reg_write_high = DT_INST_PROP_OR(n, skip_reg_write_high, false), \
		.skip_reg_write_low = DT_INST_PROP_OR(n, skip_reg_write_low, false), \
	}; \
	static struct gpio_ads1220_data gpio_ads1220_data_##n = { \
		.data = {0}, \
	}; \
	DEVICE_DT_INST_DEFINE(n, gpio_ads1220_init,	NULL,	\
			&gpio_ads1220_data_##n, &gpio_ads1220_config_##n,	\
			POST_KERNEL, CONFIG_GPIO_INIT_PRIORITY, \
			&gpio_ads1220_driver_api);

DT_INST_FOREACH_STATUS_OKAY(GPIO_ADS1220_INST_DEFINE)
