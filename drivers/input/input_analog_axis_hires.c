/*
 * Copyright 2023 Google LLC
 * Copyright 2026 badjeff
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#define DT_DRV_COMPAT analog_axis_hires

#include <stdlib.h>
#include <string.h>
#include <zephyr/device.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/input/input.h>
#include <zephyr/kernel.h>
#include <zephyr/logging/log.h>
#include <zephyr/pm/device.h>
#include <zephyr/pm/device_runtime.h>
#include <zephyr/sys/atomic.h>
#include <zephyr/sys/util.h>

LOG_MODULE_REGISTER(analog_axis_hires, CONFIG_INPUT_LOG_LEVEL);

// Headers from this module
#include <zephyr/input/input_analog_axis_hires.h>

struct analog_axis_hires_channel_config {
	struct adc_dt_spec adc;
	struct gpio_dt_spec gpio_avdd;
	bool has_gpio_avdd;
	int32_t out_min;
	int32_t out_max;
	uint16_t axis_type;
	uint16_t axis;
	bool invert_input;
	bool invert_output;
	bool skip_change_comparator;
};

struct analog_axis_hires_channel_data {
	int32_t last_out;
};

struct analog_axis_hires_config {
	uint32_t poll_period_ms;
	const struct analog_axis_hires_channel_config *channel_cfg;
	struct analog_axis_hires_channel_data *channel_data;
	struct analog_axis_hires_calibration *calibration;
	const uint8_t num_channels;
};

struct analog_axis_hires_data {
	struct k_sem cal_lock;
	analog_axis_hires_raw_data_t raw_data_cb;
	struct k_timer timer;
	struct k_thread thread;
	bool use_same_adc_ch_cfg;

	K_KERNEL_STACK_MEMBER(thread_stack,
			      CONFIG_INPUT_ANALOG_AXIS_HIRES_THREAD_STACK_SIZE);

#ifdef CONFIG_PM_DEVICE
	atomic_t suspended;
	struct k_sem wakeup;
#endif
};

int analog_axis_hires_num_axes(const struct device *dev)
{
	const struct analog_axis_hires_config *cfg = dev->config;

	return cfg->num_channels;
}

int analog_axis_hires_calibration_get(const struct device *dev,
				int channel,
				struct analog_axis_hires_calibration *out_cal)
{
	const struct analog_axis_hires_config *cfg = dev->config;
	struct analog_axis_hires_data *data = dev->data;
	struct analog_axis_hires_calibration *cal = &cfg->calibration[channel];

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	k_sem_take(&data->cal_lock, K_FOREVER);
	memcpy(out_cal, cal, sizeof(struct analog_axis_hires_calibration));
	k_sem_give(&data->cal_lock);

	return 0;
}

void analog_axis_hires_set_raw_data_cb(const struct device *dev, analog_axis_hires_raw_data_t cb)
{
	struct analog_axis_hires_data *data = dev->data;

	k_sem_take(&data->cal_lock, K_FOREVER);
	data->raw_data_cb = cb;
	k_sem_give(&data->cal_lock);
}

int analog_axis_hires_calibration_set(const struct device *dev,
				int channel,
				struct analog_axis_hires_calibration *new_cal)
{
	const struct analog_axis_hires_config *cfg = dev->config;
	struct analog_axis_hires_data *data = dev->data;
	struct analog_axis_hires_calibration *cal = &cfg->calibration[channel];

	if (channel >= cfg->num_channels) {
		return -EINVAL;
	}

	k_sem_take(&data->cal_lock, K_FOREVER);
	memcpy(cal, new_cal, sizeof(struct analog_axis_hires_calibration));
	k_sem_give(&data->cal_lock);

	return 0;
}

static int32_t analog_axis_hires_out_deadzone(const struct device *dev,
					int channel,
					int32_t raw_val)
{
	const struct analog_axis_hires_config *cfg = dev->config;
	const struct analog_axis_hires_channel_config *axis_cfg = &cfg->channel_cfg[channel];
	struct analog_axis_hires_calibration *cal = &cfg->calibration[channel];

	int32_t in_range = cal->in_max - cal->in_min;
	int32_t out_range = axis_cfg->out_max - axis_cfg->out_min;
	int32_t in_mid = DIV_ROUND_CLOSEST(cal->in_min + cal->in_max, 2);
	int32_t in_min = cal->in_min;

	if (abs(raw_val - in_mid) < cal->in_deadzone) {
		return DIV_ROUND_CLOSEST(axis_cfg->out_max + axis_cfg->out_min, 2);
	}

	in_range -= cal->in_deadzone * 2;
	in_min += cal->in_deadzone;
	if (raw_val < in_mid) {
		raw_val += cal->in_deadzone;
	} else {
		raw_val -= cal->in_deadzone;
	}

	int32_t val = raw_val - in_min;
	float in = (double)val / (double)in_range;
	int32_t out = in * out_range;
	// LOG_DBG("%d / %d >> %d / %d", val, in_range, out, out_range);
	return out + axis_cfg->out_min;
}

static int32_t analog_axis_hires_out_linear(const struct device *dev,
				      int channel,
				      int32_t raw_val)
{
	const struct analog_axis_hires_config *cfg = dev->config;
	const struct analog_axis_hires_channel_config *axis_cfg = &cfg->channel_cfg[channel];
	struct analog_axis_hires_calibration *cal = &cfg->calibration[channel];

	int32_t in_range = cal->in_max - cal->in_min;
	int32_t out_range = axis_cfg->out_max - axis_cfg->out_min;

	int32_t val = raw_val - cal->in_min;
	float in = (double)val / (double)in_range;
	int32_t out = in * out_range;
	// LOG_DBG("%d / %d >> %d / %d", val, in_range, out, out_range);
	return out + axis_cfg->out_min;
}

static void analog_axis_hires_loop(const struct device *dev)
{
	const struct analog_axis_hires_config *cfg = dev->config;
	struct analog_axis_hires_data *data = dev->data;
	int32_t bufs[cfg->num_channels];
	int32_t out;
	struct adc_sequence sequence = {
		.buffer = bufs,
		.buffer_size = sizeof(bufs),
	};
	const struct analog_axis_hires_channel_config *axis_cfg_0 = &cfg->channel_cfg[0];
	int err;
	int i;

	if (data->use_same_adc_ch_cfg && !axis_cfg_0->has_gpio_avdd) {
		adc_sequence_init_dt(&axis_cfg_0->adc, &sequence);

		for (i = 0; i < cfg->num_channels; i++) {
			const struct analog_axis_hires_channel_config *axis_cfg = &cfg->channel_cfg[i];
			sequence.channels |= BIT(axis_cfg->adc.channel_id);
		}

		err = adc_read(axis_cfg_0->adc.dev, &sequence);
		if (err < 0) {
			LOG_ERR("Could not read (%d)", err);
			return;
		}
	}
	else { // has_gpio_avdd || !use_same_adc_ch_cfg
		for (i = 0; i < cfg->num_channels; i++) {
			const struct analog_axis_hires_channel_config *axis_cfg = &cfg->channel_cfg[i];

			// LOG_DBG("c: %d has_gpio_avdd: %s", i, axis_cfg->has_gpio_avdd ? "yes" : "no");
			if (axis_cfg->has_gpio_avdd) {
				gpio_pin_set_dt(&axis_cfg->gpio_avdd, 1);
				// k_usleep(150);
			}

			if (!data->use_same_adc_ch_cfg) {
				err = adc_channel_setup_dt(&axis_cfg->adc);
				if (err < 0) {
					LOG_ERR("Could not setup channel #%d (%d)", i, err);
					if (axis_cfg->has_gpio_avdd) {
						gpio_pin_set_dt(&axis_cfg->gpio_avdd, 0);
					}
					return;
				}
			}

			sequence.buffer = &bufs[i];
			sequence.buffer_size = sizeof(bufs[i]);
			sequence.channels = BIT(axis_cfg->adc.channel_id);
			adc_sequence_init_dt(&axis_cfg->adc, &sequence);

			err = adc_read(axis_cfg->adc.dev, &sequence);
			if (err < 0) {
				LOG_ERR("Could not read channel %d (%d)", i, err);
				if (axis_cfg->has_gpio_avdd) {
					gpio_pin_set_dt(&axis_cfg->gpio_avdd, 0);
				}
				return;
			}

			if (axis_cfg->has_gpio_avdd) {
				gpio_pin_set_dt(&axis_cfg->gpio_avdd, 0);
			}
		}
	}

	k_sem_take(&data->cal_lock, K_FOREVER);

	for (i = 0; i < cfg->num_channels; i++) {
		const struct analog_axis_hires_channel_config *axis_cfg = &cfg->channel_cfg[i];
		struct analog_axis_hires_channel_data *axis_data = &cfg->channel_data[i];
		struct analog_axis_hires_calibration *cal = &cfg->calibration[i];
		int32_t raw_val = bufs[i];

		if (axis_cfg->invert_input) {
			raw_val *= -1;
		}

		if (data->raw_data_cb != NULL) {
			data->raw_data_cb(dev, i, raw_val);
		}

		// LOG_DBG("%s: ch %d: raw_val: %d", dev->name, i, raw_val);

		if (cal->in_deadzone > 0) {
			out = analog_axis_hires_out_deadzone(dev, i, raw_val);
		} else {
			out = analog_axis_hires_out_linear(dev, i, raw_val);
		}

		out = CLAMP(out, axis_cfg->out_min, axis_cfg->out_max);
		// LOG_DBG("%s: ch %d: out: %d clamp min: %d max: %d", dev->name, i, 
		// 	out, axis_cfg->out_min, axis_cfg->out_max);

		if (axis_cfg->invert_output) {
			out = axis_cfg->out_max - out;
		}

		if (axis_cfg->skip_change_comparator) {
			input_report(dev, axis_cfg->axis_type, axis_cfg->axis, out, true, K_FOREVER);
			LOG_DBG("%s: ch %d: out: %d raw: %d (raw)", dev->name, i, out, raw_val);
		}
		else {
			// LOG_DBG("%s: ch %d: out: %d %d", dev->name, i, out, axis_data->last_out);
			if (axis_data->last_out != out) {
				input_report(dev, axis_cfg->axis_type, axis_cfg->axis, out, true, K_FOREVER);
				LOG_DBG("%s: ch %d: out: %d (changed) raw: %d", dev->name, i, out, raw_val);
			}
		}
		axis_data->last_out = out;
	}

	k_sem_give(&data->cal_lock);
}

static void analog_axis_hires_thread(void *arg1, void *arg2, void *arg3)
{
	const struct device *dev = arg1;
	const struct analog_axis_hires_config *cfg = dev->config;
	struct analog_axis_hires_data *data = dev->data;
	int err;
	int i;

	LOG_INF("Analog-axis-hires thread started");

	LOG_INF("Checking if all channels use same ADC");
	struct adc_dt_spec first_adc_spec = cfg->channel_cfg[0].adc;
	struct adc_channel_cfg first_adc_ch_cfg = first_adc_spec.channel_cfg;
	const struct device *first_adc_dev = cfg->channel_cfg[0].adc.dev;
	bool same_adc = true;
	for (i = 1; i < cfg->num_channels; i++) {
		const struct analog_axis_hires_channel_config *axis_cfg = &cfg->channel_cfg[i];
		if (axis_cfg->adc.dev != first_adc_dev) {
			same_adc = false;
			break;
		} else {
			struct adc_dt_spec adc_spec = axis_cfg->adc;
			struct adc_channel_cfg adc_ch_cfg = adc_spec.channel_cfg;
			same_adc &= same_adc && adc_ch_cfg.gain == first_adc_ch_cfg.gain;
			same_adc &= same_adc && adc_ch_cfg.reference == first_adc_ch_cfg.reference;
			same_adc &= same_adc && adc_ch_cfg.acquisition_time == first_adc_ch_cfg.acquisition_time;
			same_adc &= same_adc && adc_ch_cfg.differential == first_adc_ch_cfg.differential;
			#ifdef CONFIG_ADC_CONFIGURABLE_INPUTS
			same_adc &= same_adc && adc_ch_cfg.input_positive == first_adc_ch_cfg.input_positive;
			same_adc &= same_adc && adc_ch_cfg.input_negative == first_adc_ch_cfg.input_negative;
			#endif
			#ifdef CONFIG_ADC_CONFIGURABLE_EXCITATION_CURRENT_SOURCE_PIN
			same_adc &= same_adc && adc_ch_cfg.current_source_pin_set == first_adc_ch_cfg.current_source_pin_set;
			same_adc &= same_adc && adc_ch_cfg.current_source_pin[0] == first_adc_ch_cfg.current_source_pin[0];
			same_adc &= same_adc && adc_ch_cfg.current_source_pin[1] == first_adc_ch_cfg.current_source_pin[1];
			#endif
			#ifdef CONFIG_ADC_CONFIGURABLE_VBIAS_PIN
			same_adc &= same_adc && adc_ch_cfg.vbias_pins == first_adc_ch_cfg.vbias_pins;
			#endif
			if (!same_adc) break;
		}
	}
	LOG_INF("All channels use same ADC: %s", same_adc ? "yes" : "no");
	data->use_same_adc_ch_cfg = same_adc;

	for (i = 0; i < cfg->num_channels; i++) {
		const struct analog_axis_hires_channel_config *axis_cfg = &cfg->channel_cfg[i];
		LOG_INF("Setting up channel %d, ADC channel %d", i, axis_cfg->adc.channel_id);

		if (axis_cfg->has_gpio_avdd) {
			/* Check if AVDD GPIO is defined in device tree */
			if (!device_is_ready(axis_cfg->gpio_avdd.port)) {
				LOG_ERR("AVDD GPIO not ready on channel %d", i);
				return;
			}

			err = gpio_pin_configure_dt(&axis_cfg->gpio_avdd, GPIO_OUTPUT_INACTIVE);
			if (err != 0) {
				LOG_ERR("Setup AVDD GPIO config failed on channel %d (%d)", i, err);
				return;
			}
		}

		if (!adc_is_ready_dt(&axis_cfg->adc)) {
			LOG_ERR("ADC controller device not ready on channel #%d", i);
			return;
		}

		err = adc_channel_setup_dt(&axis_cfg->adc);
		if (err < 0) {
			LOG_ERR("Could not setup channel #%d (%d)", i, err);
			return;
		}
		LOG_INF("Channel %d setup done", i);
	}

	LOG_INF("All channels setup complete, entering poll loop");

	while (true) {
#ifdef CONFIG_PM_DEVICE
		if (atomic_get(&data->suspended) == 1) {
			k_sem_take(&data->wakeup, K_FOREVER);
		}
#endif

		analog_axis_hires_loop(dev);
		k_timer_status_sync(&data->timer);
	}
}

static int analog_axis_hires_init(const struct device *dev)
{
	struct analog_axis_hires_data *data = dev->data;
	k_tid_t tid;

	LOG_INF("analog_axis_hires_init: START");

	k_sem_init(&data->cal_lock, 1, 1);
	k_timer_init(&data->timer, NULL, NULL);

#ifdef CONFIG_PM_DEVICE
	k_sem_init(&data->wakeup, 0, 1);
#endif

	tid = k_thread_create(&data->thread, data->thread_stack,
			      K_KERNEL_STACK_SIZEOF(data->thread_stack),
			      analog_axis_hires_thread, (void *)dev, NULL, NULL,
			      CONFIG_INPUT_ANALOG_AXIS_HIRES_THREAD_PRIORITY,
			      0, K_NO_WAIT);
	if (!tid) {
		LOG_ERR("thread creation failed");
		return -ENODEV;
	}

	LOG_INF("Thread created successfully, name=%s", dev->name);
	k_thread_name_set(&data->thread, dev->name);

#ifndef CONFIG_PM_DEVICE_RUNTIME
	const struct analog_axis_hires_config *cfg = dev->config;

	LOG_INF("Starting timer with period %d ms", cfg->poll_period_ms);
	k_timer_start(&data->timer,
		      K_MSEC(cfg->poll_period_ms), K_MSEC(cfg->poll_period_ms));
#else
	int ret;

	atomic_set(&data->suspended, 1);

	pm_device_init_suspended(dev);
	ret = pm_device_runtime_enable(dev);
	if (ret < 0) {
		LOG_ERR("Failed to enable runtime power management");
		return ret;
	}
#endif

	LOG_INF("Analog-axis-hires Initialised");

	return 0;
}

#ifdef CONFIG_PM_DEVICE
static int analog_axis_hires_pm_action(const struct device *dev,
				 enum pm_device_action action)
{
	const struct analog_axis_hires_config *cfg = dev->config;
	struct analog_axis_hires_data *data = dev->data;

	switch (action) {
	case PM_DEVICE_ACTION_SUSPEND:
		atomic_set(&data->suspended, 1);
		k_timer_stop(&data->timer);
		break;
	case PM_DEVICE_ACTION_RESUME:
		k_timer_start(&data->timer,
			      K_MSEC(cfg->poll_period_ms),
			      K_MSEC(cfg->poll_period_ms));
		atomic_set(&data->suspended, 0);
		k_sem_give(&data->wakeup);
		break;
	default:
		return -ENOTSUP;
	}

	return 0;
}
#endif

#define ANALOG_AXIS_CHANNEL_CFG_DEF(node_id) \
	{ \
		.adc = ADC_DT_SPEC_GET(node_id), \
		.gpio_avdd = GPIO_DT_SPEC_GET_OR(node_id, avdd_gpios, {0}), \
		.has_gpio_avdd = DT_PROP_HAS_IDX(node_id, avdd_gpios, 0), \
		.out_min = (int32_t)DT_PROP(node_id, out_min), \
		.out_max = (int32_t)DT_PROP(node_id, out_max), \
		.axis_type = DT_PROP(node_id, zephyr_axis_type), \
		.axis = DT_PROP(node_id, zephyr_axis), \
		.invert_input = DT_PROP(node_id, invert_input), \
		.invert_output = DT_PROP(node_id, invert_output), \
		.skip_change_comparator = DT_PROP(node_id, skip_change_comparator), \
	}

#define ANALOG_AXIS_CHANNEL_CAL_DEF(node_id) \
	{ \
		.in_min = (int32_t)DT_PROP(node_id, in_min), \
		.in_max = (int32_t)DT_PROP(node_id, in_max), \
		.in_deadzone = (int32_t)DT_PROP(node_id, in_deadzone), \
	}

#define ANALOG_AXIS_INIT(inst)									\
	static const struct analog_axis_hires_channel_config analog_axis_hires_channel_cfg_##inst[] = {	\
		DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(inst, ANALOG_AXIS_CHANNEL_CFG_DEF, (,))	\
	};											\
												\
	static struct analog_axis_hires_channel_data							\
		analog_axis_hires_channel_data_##inst[ARRAY_SIZE(analog_axis_hires_channel_cfg_##inst)];	\
												\
	static struct analog_axis_hires_calibration							\
		analog_axis_hires_calibration_##inst[ARRAY_SIZE(analog_axis_hires_channel_cfg_##inst)] = {	\
			DT_INST_FOREACH_CHILD_STATUS_OKAY_SEP(					\
				inst, ANALOG_AXIS_CHANNEL_CAL_DEF, (,))				\
		};										\
												\
	static const struct analog_axis_hires_config analog_axis_hires_cfg_##inst = {			\
		.poll_period_ms = DT_INST_PROP(inst, poll_period_ms),				\
		.channel_cfg = analog_axis_hires_channel_cfg_##inst,					\
		.channel_data = analog_axis_hires_channel_data_##inst,					\
		.calibration = analog_axis_hires_calibration_##inst,					\
		.num_channels = ARRAY_SIZE(analog_axis_hires_channel_cfg_##inst),			\
	};											\
												\
	static struct analog_axis_hires_data analog_axis_hires_data_##inst;					\
												\
	PM_DEVICE_DT_INST_DEFINE(inst, analog_axis_hires_pm_action);					\
												\
	DEVICE_DT_INST_DEFINE(inst, analog_axis_hires_init, PM_DEVICE_DT_INST_GET(inst),		\
			      &analog_axis_hires_data_##inst, &analog_axis_hires_cfg_##inst,		\
			      POST_KERNEL, CONFIG_INPUT_INIT_PRIORITY, NULL);

DT_INST_FOREACH_STATUS_OKAY(ANALOG_AXIS_INIT)
