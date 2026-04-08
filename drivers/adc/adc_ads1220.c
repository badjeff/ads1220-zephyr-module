/*
 * Copyright (c) 2026 badjeff
 *
 * SPDX-License-Identifier: Apache-2.0
 */

#include <zephyr/device.h>
#include <zephyr/devicetree.h>
#include <zephyr/drivers/adc.h>
#include <zephyr/drivers/spi.h>
#include <zephyr/drivers/gpio.h>
#include <zephyr/logging/log.h>
#include <zephyr/kernel.h>
#include <zephyr/pm/device.h>
#include <zephyr/sys/byteorder.h>
#include <zephyr/sys/util.h>
#include <zephyr/sys/util_macro.h>
#include <string.h>

// Headers from zephyr/drivers/adc/adc_context.h
#define ADC_CONTEXT_USES_KERNEL_TIMER
#include <adc_context.h>

LOG_MODULE_REGISTER(ads1220, CONFIG_ADC_LOG_LEVEL);

#define ADS1220_REF_INTERNAL	2048  /* Internal reference voltage in mV */
#define ADS1220_RESOLUTION		24    /* ADC resolution in bits */

#define ADS1220_CONFIG0_REG		0x00   /* Configuration register 0 */
#define ADS1220_CONFIG1_REG		0x01   /* Configuration register 1 */
#define ADS1220_CONFIG2_REG		0x02   /* Configuration register 2 */

#define ADS1220_RESET_CMD				0x06   /* Reset command */
#define ADS1220_START_CMD				0x08   /* Start/conversion command */
#define ADS1220_POWERDOWN_CMD		0x02   /* Power-down command */
#define ADS1220_RDATA_CMD				0x10   /* Read data command */
#define ADS1220_RREG_CMD				0x20   /* Read register command */
#define ADS1220_WREG_CMD				0x40   /* Write register command */

#define ADS1220_MUX_MASK		GENMASK(7, 4)  /* CONFIG0: MUX selection bits */
#define ADS1220_GAIN_MASK		GENMASK(3, 1)  /* CONFIG0: PGA gain selection bits */
#define ADS1220_DR_MASK			GENMASK(7, 5)  /* CONFIG1: Data rate selection bits */
#define ADS1220_MODE_MASK		GENMASK(2, 2)  /* CONFIG1: Conversion mode: 0=continuous, 1=single-shot */

#define ADS1220_VREF_INTERNAL				0      /* Internal 2.048V reference */
#define ADS1220_VREF_EXTERNAL_REFP0	1      /* External reference REFP0/REFN0 */
#define ADS1220_VREF_EXTERNAL_REFP1	2      /* External reference REFP1/REFN1 (AIN0/AIN3) */
#define ADS1220_VREF_AVDD						3      /* AVDD as reference */

#define ADS1220_RESET_DELAY	1      /* Reset delay in ms */

#define ADS1220_GAIN_1		0       /* Gain = 1 */
#define ADS1220_GAIN_2		1       /* Gain = 2 */
#define ADS1220_GAIN_4		2       /* Gain = 4 */
#define ADS1220_GAIN_8		3       /* Gain = 8 */
#define ADS1220_GAIN_16		4       /* Gain = 16 */
#define ADS1220_GAIN_32		5       /* Gain = 32 */
#define ADS1220_GAIN_64		6       /* Gain = 64 */
#define ADS1220_GAIN_128	7       /* Gain = 128 */

#define ADS1220_ACQ_TIME_1000			1000   /* For 1000 SPS */
#define ADS1220_ACQ_TIME_600			600    /* For 600 SPS */
#define ADS1220_ACQ_TIME_330			330    /* For 330 SPS */
#define ADS1220_ACQ_TIME_175			175    /* For 175 SPS */
#define ADS1220_ACQ_TIME_90				90     /* For 90 SPS */
#define ADS1220_ACQ_TIME_45				45     /* For 45 SPS */
#define ADS1220_ACQ_TIME_20				20     /* For 20 SPS */
#define ADS1220_ACQ_TIME_DEFAULT	ADC_ACQ_TIME_DEFAULT /* Default to ADS1220_ACQ_TIME_1000 */
#define ADS1220_ACQ_TIME_MAX			ADC_ACQ_TIME_MAX /* Max to ADS1220_ACQ_TIME_20 */

enum ads1220_input {
	ADS1220_INPUT_AIN0 = 1,
	ADS1220_INPUT_AIN1 = 2,
	ADS1220_INPUT_AIN2 = 3,
	ADS1220_INPUT_AIN3 = 4,
	ADS1220_INPUT_AVSS = 5,
	ADS1220_INPUT_REFP = 6,
	ADS1220_INPUT_AVDD = 7,
	ADS1220_INPUT_SHORT = 8,
};

enum ads1220_mux {
	ADS1220_MUX_P_AIN0_N_AIN1 = 0x00,  /* 0000: AINP=AIN0, AINN=AIN1 */
	ADS1220_MUX_P_AIN0_N_AIN2 = 0x01,  /* 0001: AINP=AIN0, AINN=AIN2 */
	ADS1220_MUX_P_AIN0_N_AIN3 = 0x02,  /* 0010: AINP=AIN0, AINN=AIN3 */
	ADS1220_MUX_P_AIN1_N_AIN2 = 0x03,  /* 0011: AINP=AIN1, AINN=AIN2 */
	ADS1220_MUX_P_AIN1_N_AIN3 = 0x04,  /* 0100: AINP=AIN1, AINN=AIN3 */
	ADS1220_MUX_P_AIN2_N_AIN3 = 0x05,  /* 0101: AINP=AIN2, AINN=AIN3 */
	ADS1220_MUX_P_AIN1_N_AIN0 = 0x06,  /* 0110: AINP=AIN1, AINN=AIN0 */
	ADS1220_MUX_P_AIN3_N_AIN2 = 0x07,  /* 0111: AINP=AIN3, AINN=AIN2 */
	ADS1220_MUX_P_AIN0_N_AVSS = 0x08,  /* 1000: AINP=AIN0, AINN=AVSS */
	ADS1220_MUX_P_AIN1_N_AVSS = 0x09,  /* 1001: AINP=AIN1, AINN=AVSS */
	ADS1220_MUX_P_AIN2_N_AVSS = 0x0A,  /* 1010: AINP=AIN2, AINN=AVSS */
	ADS1220_MUX_P_AIN3_N_AVSS = 0x0B,  /* 1011: AINP=AIN3, AINN=AVSS */
	ADS1220_MUX_P_REFP_N_REFN = 0x0C,  /* 1100: (V(REFPx)-V(REFNx))/4 monitor */
	ADS1220_MUX_P_AVDD_N_AVSS = 0x0D,  /* 1101: (AVDD-AVSS)/4 monitor */
	ADS1220_MUX_P_N_SHORT = 0x0E,      /* 1110: AINP and AINN shorted */
};

enum ads1220_data_rate {
	ADS1220_DR_20 = 0x00,    /* 000: 20 SPS */
	ADS1220_DR_45 = 0x20,    /* 001: 45 SPS */
	ADS1220_DR_90 = 0x40,    /* 010: 90 SPS */
	ADS1220_DR_175 = 0x60,   /* 011: 175 SPS */
	ADS1220_DR_330 = 0x80,   /* 100: 330 SPS */
	ADS1220_DR_600 = 0xA0,   /* 101: 600 SPS */
	ADS1220_DR_1000 = 0xC0,  /* 110: 1000 SPS */
};

struct ads1220_config {
	const struct spi_dt_spec spi;
	const struct gpio_dt_spec gpio_drdy;
	bool low_side_power_switch;
};

struct ads1220_data {
	struct adc_context ctx;
	struct k_sem acq_sem;
	struct k_sem drdy_sem;
	struct gpio_callback callback_drdy;
	int32_t *buffer;
	int32_t *buffer_ptr;
	bool has_drdy;
	k_timeout_t ready_time;
	uint8_t last_config0;
	uint8_t last_config1;
	uint8_t last_config2;
};

static inline int ads1220_transceive(const struct device *dev,
				       uint8_t *send_buf, size_t send_buf_len,
				       uint8_t *recv_buf, size_t recv_buf_len)
{
	const struct ads1220_config *cfg = dev->config;

	struct spi_buf tx_buf = {
		.buf = send_buf,
		.len = send_buf_len,
	};
	const struct spi_buf_set tx = {
		.buffers = &tx_buf,
		.count = 1
	};

	struct spi_buf rx_buf = {
		.buf = recv_buf,
		.len = send_buf_len,
	};
	const struct spi_buf_set rx = {
		.buffers = &rx_buf,
		.count = 1,
	};

	return spi_transceive_dt(&cfg->spi, &tx, &rx);
}

static int ads1220_reg_read(const struct device *dev, uint8_t addr,
			      uint8_t *read_buf, size_t read_buf_len)
{
	uint8_t tx_buf[2] = { ADS1220_RREG_CMD | (addr << 2), 0x00 };
	uint8_t rx_buf[2] = { 0 };
	int ret;

	ret = ads1220_transceive(dev, tx_buf, 2, rx_buf, 2);
	if (ret == 0 && read_buf_len > 0) {
		*read_buf = rx_buf[1];
	}
	return ret;
}

static int ads1220_reg_write(const struct device *dev, uint8_t addr,
			       uint8_t write_data)
{
	uint8_t tx_buf[2] = { ADS1220_WREG_CMD | (addr << 2), write_data };
	uint8_t rx_buf[2] = { 0 };

	return ads1220_transceive(dev, tx_buf, 2, rx_buf, 2);
}

static int ads1220_command(const struct device *dev, uint8_t cmd)
{
	uint8_t tx_buf[1] = { cmd };
	uint8_t rx_buf[1] = { 0 };

	return ads1220_transceive(dev, tx_buf, 1, rx_buf, 1);
}

static int ads1220_setup(const struct device *dev,
			   const struct adc_channel_cfg *channel_cfg)
{
	int ret;
	uint8_t config0;
	uint8_t config1;
	uint8_t config2;
	uint8_t gain;
	uint8_t mux_value;
	uint8_t data_rate = ADS1220_DR_1000;
	uint16_t acq_time = channel_cfg->acquisition_time;
	uint32_t ready_time_us = 1000;
	uint8_t vref_value = ADS1220_VREF_INTERNAL;
	uint8_t psw_value = 0;
	const struct ads1220_config *cfg = dev->config;
	struct ads1220_data *data = dev->data;

	config0 = data->last_config0;
	config1 = data->last_config1;
	config2 = data->last_config2;
	// LOG_DBG("setup: last active CONFIG0/1/2=0x%02X / 0x%02X / 0x%02X", 
	// 	config0, config1, config2);

	gain = channel_cfg->gain;

	switch (gain) {
	case ADC_GAIN_1:
		gain = ADS1220_GAIN_1;
		break;
	case ADC_GAIN_2:
		gain = ADS1220_GAIN_2;
		break;
	case ADC_GAIN_4:
		gain = ADS1220_GAIN_4;
		break;
	case ADC_GAIN_8:
		gain = ADS1220_GAIN_8;
		break;
	case ADC_GAIN_16:
		gain = ADS1220_GAIN_16;
		break;
	case ADC_GAIN_32:
		gain = ADS1220_GAIN_32;
		break;
	case ADC_GAIN_64:
		gain = ADS1220_GAIN_64;
		break;
	case ADC_GAIN_128:
		gain = ADS1220_GAIN_128;
		break;
	default:
		LOG_WRN("Invalid given gain: %d. fallback to ADC_GAIN_1", gain);
		gain = ADS1220_GAIN_1;
		break;
	}

	uint8_t pos_input = channel_cfg->input_positive;
	uint8_t neg_input = channel_cfg->input_negative;
	mux_value = 0;

	switch (pos_input) {
	case ADS1220_INPUT_AIN0:
		if (neg_input == ADS1220_INPUT_AIN1) {
			mux_value = ADS1220_MUX_P_AIN0_N_AIN1;
		} else if (neg_input == ADS1220_INPUT_AIN2) {
			mux_value = ADS1220_MUX_P_AIN0_N_AIN2;
		} else if (neg_input == ADS1220_INPUT_AIN3) {
			mux_value = ADS1220_MUX_P_AIN0_N_AIN3;
		} else if (neg_input == ADS1220_INPUT_AVSS) {
			mux_value = ADS1220_MUX_P_AIN0_N_AVSS;
		} else {
			return -EINVAL;
		}
		break;
	case ADS1220_INPUT_AIN1:
		if (neg_input == ADS1220_INPUT_AIN0) {
			mux_value = ADS1220_MUX_P_AIN1_N_AIN0;
		} else if (neg_input == ADS1220_INPUT_AIN2) {
			mux_value = ADS1220_MUX_P_AIN1_N_AIN2;
		} else if (neg_input == ADS1220_INPUT_AIN3) {
			mux_value = ADS1220_MUX_P_AIN1_N_AIN3;
		} else if (neg_input == ADS1220_INPUT_AVSS) {
			mux_value = ADS1220_MUX_P_AIN1_N_AVSS;
		} else {
			return -EINVAL;
		}
		break;
	case ADS1220_INPUT_AIN2:
		if (neg_input == ADS1220_INPUT_AIN3) {
			mux_value = ADS1220_MUX_P_AIN2_N_AIN3;
		} else if (neg_input == ADS1220_INPUT_AVSS) {
			mux_value = ADS1220_MUX_P_AIN2_N_AVSS;
		} else {
			return -EINVAL;
		}
		break;
	case ADS1220_INPUT_AIN3:
		if (neg_input == ADS1220_INPUT_AIN2) {
			mux_value = ADS1220_MUX_P_AIN3_N_AIN2;
		} else if (neg_input == ADS1220_INPUT_AVSS) {
			mux_value = ADS1220_MUX_P_AIN3_N_AVSS;
		} else {
			return -EINVAL;
		}
		break;
	case ADS1220_INPUT_REFP:
		mux_value = ADS1220_MUX_P_REFP_N_REFN;
		break;
	case ADS1220_INPUT_AVDD:
		mux_value = ADS1220_MUX_P_AVDD_N_AVSS;
		break;
	case ADS1220_INPUT_SHORT:
		mux_value = ADS1220_MUX_P_N_SHORT;
		break;
	default:
		return -EINVAL;
	}

	config0 = (config0 & ~(ADS1220_MUX_MASK | ADS1220_GAIN_MASK)) |
		   FIELD_PREP(ADS1220_MUX_MASK, mux_value) |
		   FIELD_PREP(ADS1220_GAIN_MASK, gain);
	// LOG_DBG("CONFIG0: MUX=0x%02X, GAIN=0x%02X",
	// 	(unsigned int)(config0 & ADS1220_MUX_MASK),
	// 	(unsigned int)(config0 & ADS1220_GAIN_MASK));

	if (acq_time == ADC_ACQ_TIME_DEFAULT) {
		data_rate = ADS1220_DR_1000;
		ready_time_us = 1000 * 1000 / 1000;
	} else if (acq_time == ADC_ACQ_TIME_MAX) {
		data_rate = ADS1220_DR_20;
		ready_time_us = 1000 * 1000 / 20;
	} else {
		switch (acq_time) {
		case ADS1220_ACQ_TIME_1000:
			data_rate = ADS1220_DR_1000;
			ready_time_us = 1000 * 1000 / 1000;
			break;
		case ADS1220_ACQ_TIME_600:
			data_rate = ADS1220_DR_600;
			ready_time_us = 1000 * 1000 / 600;
			break;
		case ADS1220_ACQ_TIME_330:
			data_rate = ADS1220_DR_330;
			ready_time_us = 1000 * 1000 / 330;
			break;
		case ADS1220_ACQ_TIME_175:
			data_rate = ADS1220_DR_175;
			ready_time_us = 1000 * 1000 / 175;
			break;
		case ADS1220_ACQ_TIME_90:
			data_rate = ADS1220_DR_90;
			ready_time_us = 1000 * 1000 / 90;
			break;
		case ADS1220_ACQ_TIME_45:
			data_rate = ADS1220_DR_45;
			ready_time_us = 1000 * 1000 / 45;
			break;
		case ADS1220_ACQ_TIME_20:
			data_rate = ADS1220_DR_20;
			ready_time_us = 1000 * 1000 / 20;
			break;
		default:
			data_rate = ADS1220_DR_1000;
			ready_time_us = 1000;
			break;
		}
	}

	config1 = (config1 & ~(ADS1220_DR_MASK | ADS1220_MODE_MASK)) |
		  data_rate | FIELD_PREP(ADS1220_MODE_MASK, 1);
	data->ready_time = K_USEC(ready_time_us + (ready_time_us / 10));
	// LOG_DBG("CONFIG1: DR=0x%02X", (unsigned int)(config1 & ADS1220_DR_MASK));

	switch (channel_cfg->reference) {
	case ADC_REF_INTERNAL:
		vref_value = ADS1220_VREF_INTERNAL;
		break;
	case ADC_REF_EXTERNAL0:
		vref_value = ADS1220_VREF_EXTERNAL_REFP0;
		break;
	case ADC_REF_EXTERNAL1:
		vref_value = ADS1220_VREF_EXTERNAL_REFP1;
		break;
	case ADC_REF_VDD_1:
		vref_value = ADS1220_VREF_AVDD;
		break;
	default:
		// LOG_DBG("Unsupported reference, using default internal");
		vref_value = ADS1220_VREF_INTERNAL;
		break;
	}

	if (cfg->low_side_power_switch) {
		psw_value = BIT(3);
	}

	config2 = (config2 & ~0x30) | (vref_value << 4);
	config2 = (config2 & ~0x08) | psw_value;
	// LOG_DBG("CONFIG2: VREF=0x%02X, PSW=%d",
	// 	(config2 >> 4) & 0x03, (config2 >> 3) & 0x01);

	uint8_t read_config0, read_config1, read_config2;

	if (config0 != data->last_config0) {
		ret = ads1220_reg_write(dev, ADS1220_CONFIG0_REG, config0);
		if (ret < 0) {
			return ret;
		}
		ads1220_reg_read(dev, ADS1220_CONFIG0_REG, &read_config0, 1);
		if (read_config0 != config0) {
			LOG_ERR("config0 mismatch! 0x%02X != 0x%02X", config0, read_config0);
			return -EIO;
		} else {
			// LOG_DBG("wrote CONFIG0=0x%02X", read_config0);
			data->last_config0 = config0;
		}
	}

	if (config1 != data->last_config1) {
		ret = ads1220_reg_write(dev, ADS1220_CONFIG1_REG, config1);
		if (ret < 0) {
			return ret;
		}
		ads1220_reg_read(dev, ADS1220_CONFIG1_REG, &read_config1, 1);
		if (read_config1 != config1) {
			LOG_ERR("config1 mismatch! 0x%02X != 0x%02X", config1, read_config1);
			return -EIO;
		} else {
			// LOG_DBG("wrote CONFIG1=0x%02X", read_config1);
			data->last_config1 = config1;
		}
	}

	if (config2 != data->last_config2) {
		ret = ads1220_reg_write(dev, ADS1220_CONFIG2_REG, config2);
		if (ret < 0) {
			return ret;
		}
		ads1220_reg_read(dev, ADS1220_CONFIG2_REG, &read_config2, 1);
		if (read_config2 != config2) {
			LOG_ERR("config2 mismatch! 0x%02X != 0x%02X", config2, read_config2);
			return -EIO;
		} else {
			// LOG_DBG("wrote CONFIG2=0x%02X", read_config2);
			data->last_config2 = config2;
		}
	}

	return 0;
}

static int ads1220_channel_setup(const struct device *dev,
				   const struct adc_channel_cfg *channel_cfg)
{
	return ads1220_setup(dev, channel_cfg);
}

static int ads1220_validate_buffer_size(const struct adc_sequence *sequence)
{
	size_t needed = sizeof(int32_t);

	if (sequence->options) {
		needed *= (1 + sequence->options->extra_samplings);
	}

	if (sequence->buffer_size < needed) {
		LOG_ERR("sequence buffer_size < needed: %d < %d", 
			sequence->buffer_size, needed);
		return -ENOMEM;
	}

	return 0;
}

static int ads1220_validate_sequence(const struct adc_sequence *sequence)
{
	if (sequence->resolution != ADS1220_RESOLUTION) {
		LOG_ERR("sequence resolution: %d (!= %d)", 
			sequence->resolution, ADS1220_RESOLUTION);
		return -EINVAL;
	}

	if (sequence->channels == 0 || (sequence->channels & ~0x0F) != 0) {
		LOG_ERR("invalid channel");
		return -EINVAL;
	}

	if (sequence->oversampling) {
		LOG_ERR("oversampling is not supported."
						" Use built-in configurable filter via data rate.");
		return -EINVAL;
	}

	return ads1220_validate_buffer_size(sequence);
}

static void adc_context_update_buffer_pointer(struct adc_context *ctx,
					      bool repeat_sampling)
{
	struct ads1220_data *data = CONTAINER_OF(ctx, struct ads1220_data, ctx);

	if (repeat_sampling) {
		data->buffer = data->buffer_ptr;
	}
}

static void adc_context_start_sampling(struct adc_context *ctx)
{
	struct ads1220_data *data = CONTAINER_OF(ctx, struct ads1220_data, ctx);

	data->buffer_ptr = data->buffer;
	k_sem_give(&data->acq_sem);
}

static int ads1220_adc_start_read(const struct device *dev,
				    const struct adc_sequence *sequence,
				    bool wait)
{
	int ret;
	struct ads1220_data *data = dev->data;

	ret = ads1220_validate_sequence(sequence);
	if (ret != 0) {
		LOG_ERR("sequence validation failed");
		return ret;
	}

	data->buffer = sequence->buffer;
	adc_context_start_read(&data->ctx, sequence);
	if (wait) {
		ret = adc_context_wait_for_completion(&data->ctx);
	}

	return ret;
}

static int ads1220_wait_drdy(const struct device *dev)
{
	struct ads1220_data *data = dev->data;

	if (data->has_drdy) {
		return k_sem_take(&data->drdy_sem,
				  ADC_CONTEXT_WAIT_FOR_COMPLETION_TIMEOUT);
	} else {
		/* No DRDY GPIO - use timed polling based on data rate */
		k_sleep(data->ready_time);
		return 0;
	}
}

static int ads1220_read_sample(const struct device *dev,
				 uint32_t channels, uint32_t *buffer)
{
	int ret;
	uint8_t tx_buf[3] = { ADS1220_RDATA_CMD, 0, 0 };
	uint8_t rx_buf[3] = { 0 };

	ARG_UNUSED(channels);

	ret = ads1220_transceive(dev, tx_buf, 3, rx_buf, 3);
	if (ret != 0) {
		return ret;
	}

	*buffer = (int32_t)sys_get_be24(rx_buf);
	if (*buffer & 0x00800000) {
		*buffer |= 0xFF000000;
	}

	// LOG_HEXDUMP_DBG(rx_buf, 3, "raw sample");

	return 0;
}

static int ads1220_perform_read(const struct device *dev,
				  const struct adc_sequence *sequence)
{
	int ret;
	struct ads1220_data *data = dev->data;

	ret = k_sem_take(&data->acq_sem, ADC_CONTEXT_WAIT_FOR_COMPLETION_TIMEOUT);
	if (ret != 0) {
		adc_context_complete(&data->ctx, ret);
		return ret;
	}

	if (data->has_drdy) {
		k_sem_reset(&data->drdy_sem);
	}

	ret = ads1220_command(dev, ADS1220_START_CMD);
	if (ret != 0) {
		goto error;
	}

	ret = ads1220_wait_drdy(dev);
	if (ret != 0) {
		goto error;
	}

	ret = ads1220_read_sample(dev, sequence->channels, data->buffer);
	if (ret != 0) {
		goto error;
	}

	data->buffer++;
	adc_context_on_sampling_done(&data->ctx, dev);

	return 0;
error:
	adc_context_complete(&data->ctx, ret);
	return ret;
}

static int ads1220_read(const struct device *dev,
			  const struct adc_sequence *seq)
{
	int ret;
	struct ads1220_data *data = dev->data;

	adc_context_lock(&data->ctx, false, NULL);
	ret = ads1220_adc_start_read(dev, seq, false);

	while (ret == 0 && k_sem_take(&data->ctx.sync, K_NO_WAIT) != 0) {
		ret = ads1220_perform_read(dev, seq);
	}

	adc_context_release(&data->ctx, ret);

	return ret;
}

static void ads1220_data_ready_handler(const struct device *dev,
					 struct gpio_callback *gpio_cb,
					 uint32_t pins)
{
	ARG_UNUSED(dev);
	ARG_UNUSED(pins);

	struct ads1220_data *data = CONTAINER_OF(gpio_cb,
				      struct ads1220_data, callback_drdy);

	k_sem_give(&data->drdy_sem);
}

static int ads1220_configure_gpio(const struct device *dev)
{
	int ret;
	const struct ads1220_config *cfg = dev->config;
	struct ads1220_data *data = dev->data;

	/* Check if DRDY GPIO is defined in device tree */
	if (!device_is_ready(cfg->gpio_drdy.port)) {
		LOG_WRN("DRDY GPIO not configured, using timed polling");
		data->has_drdy = false;
		return 0;
	}

	ret = gpio_pin_configure_dt(&cfg->gpio_drdy, GPIO_INPUT);
	if (ret != 0) {
		LOG_WRN("DRDY GPIO config failed (%d), using timed polling", ret);
		data->has_drdy = false;
		return 0;
	}

	ret = gpio_pin_interrupt_configure_dt(&cfg->gpio_drdy,
					      GPIO_INT_EDGE_TO_ACTIVE);
	if (ret != 0) {
		LOG_WRN("DRDY GPIO interrupt config failed (%d), using timed polling", ret);
		data->has_drdy = false;
		return 0;
	}

	gpio_init_callback(&data->callback_drdy, ads1220_data_ready_handler,
			   BIT(cfg->gpio_drdy.pin));

	ret = gpio_add_callback(cfg->gpio_drdy.port, &data->callback_drdy);
	if (ret != 0) {
		LOG_WRN("DRDY GPIO callback add failed (%d), using timed polling", ret);
		data->has_drdy = false;
		return 0;
	}

	data->has_drdy = true;
	LOG_INF("DRDY GPIO configured, using hardware interrupt");

	return 0;
}

static int ads1220_device_reset(const struct device *dev)
{
	int ret;
	uint8_t tx_buf[1] = { ADS1220_RESET_CMD };
	uint8_t rx_buf[1] = { 0 };

	ret = ads1220_transceive(dev, tx_buf, 1, rx_buf, 1);
	if (ret != 0) {
		return ret;
	}

	k_msleep(ADS1220_RESET_DELAY);

	return 0;
}

#if defined CONFIG_PM_DEVICE
static int ads1220_pm_action(const struct device *dev,
			       enum pm_device_action action)
{
	switch (action) {
	case PM_DEVICE_ACTION_RESUME:
		return ads1220_command(dev, ADS1220_START_CMD);
	case PM_DEVICE_ACTION_SUSPEND:
		return ads1220_command(dev, ADS1220_POWERDOWN_CMD);
	default:
		return -EINVAL;
	}
}
#endif /* CONFIG_PM_DEVICE */

static DEVICE_API(adc, ads1220_api) = {
	.channel_setup = ads1220_channel_setup,
	.read = ads1220_read,
	.ref_internal = ADS1220_REF_INTERNAL,
};

static int ads1220_init(const struct device *dev)
{
	int ret;
	const struct ads1220_config *cfg = dev->config;
	struct ads1220_data *data = dev->data;

	if (!spi_is_ready_dt(&cfg->spi)) {
		LOG_ERR("is not ready");
		return -ENODEV;
	}

	adc_context_init(&data->ctx);
	k_sem_init(&data->acq_sem, 0, 1);
	k_sem_init(&data->drdy_sem, 0, 1);

	data->has_drdy = false;
	data->ready_time = K_USEC(1000);
	data->last_config0 = 0x00;
	data->last_config1 = 0x00;
	data->last_config2 = 0x00;

	ret = ads1220_configure_gpio(dev);
	if (ret != 0) {
		LOG_WRN("GPIO configure returned error %d", ret);
	}

	ret = ads1220_device_reset(dev);
	if (ret != 0) {
		LOG_WRN("Device is not reset");
	}

	adc_context_unlock_unconditionally(&data->ctx);

	LOG_INF("Initialised (DRDY: %s)", data->has_drdy ? "yes" : "no");

	return 0;
}

#define DT_DRV_COMPAT ti_ads1220

#define ADC_ADS1220_SPI_MODE (SPI_OP_MODE_MASTER | SPI_MODE_CPHA | SPI_WORD_SET(8))

#define ADC_ADS1220_INST_DEFINE(n)						\
	PM_DEVICE_DT_INST_DEFINE(n, ads1220_pm_action);			\
	static const struct ads1220_config config_##n = {			\
		.spi = SPI_DT_SPEC_INST_GET(n, ADC_ADS1220_SPI_MODE, 0),	\
		.gpio_drdy = GPIO_DT_SPEC_INST_GET_OR(n, drdy_gpios, {0}),	\
		.low_side_power_switch = DT_INST_PROP(n, low_side_power_switch),	\
	};									\
	static struct ads1220_data data_##n;					\
	DEVICE_DT_INST_DEFINE(n, ads1220_init,				\
			      PM_DEVICE_DT_INST_GET(n),			\
			      &data_##n, &config_##n, POST_KERNEL,		\
			      CONFIG_ADC_INIT_PRIORITY, &ads1220_api);

DT_INST_FOREACH_STATUS_OKAY(ADC_ADS1220_INST_DEFINE)
