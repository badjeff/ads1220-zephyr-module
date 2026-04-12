# ADS1220 ADC Module

This module provides two drivers for high-resolution analog input:

1. **ADS1220 ADC Driver** - Texas Instruments 24-bit SPI ADC with 4 differential input channels, configurable gain (1-128x), multiple data rates (20-1000 SPS), and optional DRDY interrupt support.

2. **Analog Axis Hi-Res Input Driver** - Fork of Zephyr's `analog-axis` driver with 24-bit buffering, per-channel auto-calibration, and multi-configuration per device. Provides all other inherited features from original driver.

## Features

### ADS1220 ADC Driver
- 24-bit resolution
- 4 differential input channels (AIN0-AIN3)
- Configurable gain: 1x, 2x, 4x, 8x, 16x, 32x, 64x, 128x
- Data rates: 20, 45, 90, 175, 330, 600, 1000 SPS
- Internal or external reference support
- Optional DRDY GPIO interrupt (fallback to timed polling)
- Low-side power switch for RTD/load cell applications
- Programmable IDAC excitation current (0/10/50/100/250/500/1000/2000 uA)
- Configurable IDAC1/IDAC2 output pins per channel

### Analog Axis Hi-Res Input Driver
- High-resolution ADC support (16-bit+)
- int32_t buffer for 24-bit ADC values
- Multi-configuration per device
- Per-channel auto-calibration support
- Other inherited features from original driver

## Installation

Only GitHub actions builds are covered here. Local builds are different for each user, therefore it's not possible to cover all cases.

Include this project on your west manifest in `config/west.yml`:

```yml
manifest:
  remotes:
    ...
    # START #####
    - name: badjeff
      url-base: https://github.com/badjeff
    # END #######
  projects:
    ...
    # START #####
    - name: ads1220-zephyr-module
      remote: badjeff
      revision: main
    # END #######
    ...
```

Now, update your `board.overlay` adding the necessary bits (update the pins for your board accordingly):

```dts
&pinctrl {
    spi2_default: spi2_default {
        group1 {
            psels = <NRF_PSEL(SPIM_SCK, 0, 8)>,
                    <NRF_PSEL(SPIM_MOSI, 0, 17)>,
                    <NRF_PSEL(SPIM_MISO, 0, 20)>;
        };
    };
    spi2_sleep: spi2_sleep {
        group1 {
            psels = <NRF_PSEL(SPIM_SCK, 0, 8)>,
                    <NRF_PSEL(SPIM_MOSI, 0, 17)>,
                    <NRF_PSEL(SPIM_MISO, 0, 20)>;
            low-power-enable;
        };
    };
};

#include <dt-bindings/spi/spi.h>

&spi2 {
    compatible = "nordic,nrf-spim";
    status = "okay";
    pinctrl-0 = <&spi2_default>;
    pinctrl-1 = <&spi2_sleep>;
    pinctrl-names = "default", "sleep";
    cs-gpios = <&gpio1 4 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;

    adc_ads1220: adc_ads1220@0 {
        compatible = "ti,ads1220";
        status = "okay";
        spi-max-frequency = <1000000>;
        reg = <0>;
        #io-channel-cells = <1>;
        #address-cells = <1>;
        #size-cells = <0>;

        /*
        Enable to wait for DRDY irq (Optional)
        If disabled, fallback to estimated sampling time base on data rate
        */
        // drdy-gpios = <&gpio1 6 (GPIO_ACTIVE_LOW | GPIO_PULL_UP)>;
        
        /* enable closing low side power switch during a measurement */
        // low-side-power-switch;

        /* IDAC excitation current in microamperes (0/10/50/100/250/500/1000/2000) */
        // idac-ua = <500>;

        /*
        Setup for Wheatstone bridges
        ref: https://wolles-elektronikkiste.de/en/ads1220-part-2-applications
        REFP1: Load cell Wire RED & AVCC
        AIN1:  Load cell Wire Green
        AIN2:  Load cell Wire White
        REFN1: Load cell Wire Black & GND
        */
        adc_ads1220_ch0: channel@0 {
            reg = <0>;
            zephyr,resolution = <24>;
            zephyr,gain = "ADC_GAIN_128";
            zephyr,reference = "ADC_REF_EXTERNAL1";
            zephyr,acquisition-time = <175>; // 175 SPS > 125 Hz
            zephyr,input-positive = <1>; // AIN1
            zephyr,input-negative = <2>; // AIN2
            /*
            IDAC1/IDAC2 output pin configuration per channel.
            Requires CONFIG_ADC_CONFIGURABLE_EXCITATION_CURRENT_SOURCE_PIN.
            Example: IDAC1 to AIN0 (01), IDAC2 disabled (00)
            */
            // zephyr,current-source-pin = [01 00];
        };
    };
};

#include <zephyr/dt-bindings/input/input-event-codes.h>

/{
    /*
    Setup the forked version of Zephyr's `analog-axis` input driver,
    'hires' means 24 bit buffering.
    Literally, enlarged all int16_t to int32_t.
    */
    anin0: analog_axis_hires_0 {
        compatible = "analog-axis-hires";
        status = "okay";

        /* Set polling for bluetooth (max rate: 7.5ms) */
        poll-period-ms = <8>; // 8ms = ~125 Hz < 175 SPS

        axis-y {
            io-channels = <&adc_ads1220 0>;

            /* netural raw value from Wheatstone bridges via ADS1220 */
            /* NOTE: depends on zephyr,gain in adc_ads1220_ch0 */
            in-min = <329030>;
            in-max = <1930010>;
            in-deadzone = <5>;

            /* clamp output */
            out-min = <( -1024 )>;
            out-max = <( 65535 )>;
            // skip-change-comparator; /* uncomment this to debug */

            zephyr,axis-type = <INPUT_EV_ABS>;
            zephyr,axis = <INPUT_ABS_Y>;
        };
    };
};
```

Now enable the driver config in your `<shield>.conf` file:

```conf
# Enable ADS1220
CONFIG_SPI=y
CONFIG_GPIO=y
CONFIG_ADC=y
CONFIG_ADC_ADS1220=y
CONFIG_ADC_LOG_LEVEL_DBG=y

# Enable Analog Axis Hi-Res Input
CONFIG_INPUT=y
CONFIG_INPUT_LOG_LEVEL_DBG=y
CONFIG_INPUT_ANALOG_AXIS_HIRES=y
CONFIG_INPUT_ANALOG_AXIS_HIRES_SETTINGS=y

# Enable threading for poll mode
CONFIG_MULTITHREADING=y

# Optional: Power management
CONFIG_PM_DEVICE=y
CONFIG_PM_DEVICE_RUNTIME=y
```

## Configuration Options

### ADS1220 Kconfig (`drivers/adc/Kconfig.ads1220`)
| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_ADC_ADS1220` | Enable ADS1220 driver | y |

### Analog Axis Hi-Res Kconfig (`drivers/input/Kconfig.analog_axis_hires`)
| Option | Description | Default |
|--------|-------------|---------|
| `CONFIG_INPUT_ANALOG_AXIS_HIRES` | Enable analog axis hires driver | y |
| `CONFIG_INPUT_ANALOG_AXIS_HIRES_THREAD_STACK_SIZE` | Thread stack size | 762 |
| `CONFIG_INPUT_ANALOG_AXIS_HIRES_THREAD_PRIORITY` | Thread priority | 0 |
| `CONFIG_INPUT_ANALOG_AXIS_HIRES_SETTINGS` | Enable settings support | y |
| `CONFIG_INPUT_ANALOG_AXIS_HIRES_SETTINGS_MAX_AXES` | Max axes for settings | 8 |

## Device Tree Properties

### ADS1220 Node (`ti,ads1220`)
| Property | Type | Description |
|----------|------|-------------|
| `spi-max-frequency` | int | SPI clock frequency |
| `drdy-gpios` | phandle-array | Data ready GPIO (optional) |
| `low-side-power-switch` | boolean | Enable low-side power switch |
| `idac-ua` | int | IDAC current (0/10/50/100/250/500/1000/2000 uA), default 0 (disabled) |

### Channel Node
| Property | Type | Description |
|----------|------|-------------|
| `zephyr,resolution` | int | ADC resolution (24) |
| `zephyr,gain` | string | PGA gain (ADC_GAIN_1/2/4/8/16/32/64/128) |
| `zephyr,reference` | string | Reference (ADC_REF_INTERNAL/EXTERNAL0/EXTERNAL1/VDD_1) |
| `zephyr,acquisition-time` | int | Data rate (ADC_ACQ_TIME_DEFAULT/1000/600/330/175/90/45/20/ADC_ACQ_TIME_MAX) |
| `zephyr,input-positive` | int | Positive input channel (0..3: AIN0..3 / 4:AVSS / 5:REFP / 6:AVDD / 7:SHORT) |
| `zephyr,input-negative` | int | Negative input channel (0..3: AIN0..3 / 4:AVSS / 5:REFP / 6:AVDD / 7:SHORT) |
| `zephyr,current-source-pin` | uint8-array | IDAC1/IDAC2 connection [IDAC1, IDAC2], requires `CONFIG_ADC_CONFIGURABLE_EXCITATION_CURRENT_SOURCE_PIN` |

### Analog Axis Hi-Res Node (`analog-axis-hires`)
| Property | Type | Description |
|----------|------|-------------|
| `poll-period-ms` | int | Poll period in milliseconds |

### Axis Child Node
| Property | Type | Description |
|----------|------|-------------|
| `io-channels` | phandle-array | ADC channel reference |
| `avdd-gpios` | phandle-array | Analog power supply gpio |
| `in-min` | int | Input min value (required) |
| `in-max` | int | Input max value (required) |
| `out-min` | int | Output min value |
| `out-max` | int | Output max value |
| `in-deadzone` | int | Input deadzone |
| `zephyr,axis-type` | int | Input event type (INPUT_EV_*) |
| `zephyr,axis` | int | Input event code (INPUT_ABS_*, INPUT_REL_*) |
| `invert-input` | boolean | Invert raw ADC value |
| `invert-output` | boolean | Invert output value |
| `skip-change-comparator` | boolean | Report every polled sample |
| `in-calib-cycle` | int | Number of samples for auto-calibration (optional) |
| `in-deadzone-calib-scale-pctg` | int | Percentage of calibration range to use as deadzone (optional, 0 = keep DT value) |

## Usage Notes

1. **Wheatstone Bridge Configuration**: For load cell applications, configure the ADS1220 with differential inputs between the bridge terminals. Use external reference (REFP1/REFN1) for better accuracy.

2. **Calibration**: The `analog-axis-hires` driver reads `in-min` and `in-max` from device tree for initial calibration. Runtime calibration can be performed through the public API.

3. **DRDY Interrupt**: If the DRDY GPIO is not configured, the driver uses timed polling based on the configured data rate (with 10% margin).

4. **Multi-Configuration Per Device**: The input driver supports multiple different configuration per device. It automatically detects if all axes share the same ADC configuration (gain, reference, acquisition time, etc.). If they do, it uses a single ADC sequence for efficient reading. If configurations differ, it re-sets up device configuration for each axis individually before reading.

5. **Power Consumption**: The poll mode driver continuously samples. For battery-powered devices, consider reducing `poll-period-ms` or implementing dynamic polling based on activity.

6. **IDAC Current Sources**: The ADS1220 provides two programmable current sources (IDAC1/IDAC2) for exciting sensors like RTDs. Use `idac-ua` to set the current level (10-2000 uA) and `zephyr,current-source-pin` to configure which pins the IDACs are connected to:
   - 0: Disabled
   - 1: AIN0/REFP1
   - 2: AIN1
   - 3: AIN2
   - 4: AIN3/REFN1
   - 5: REFP0
   - 6: REFN0

## License

Apache License - Version 2.0
