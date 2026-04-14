# Copyright (c) 2026 badjeff
# SPDX-License-Identifier: Apache-2.0

add_subdirectory_ifdef(CONFIG_ADC adc)
add_subdirectory_ifdef(CONFIG_GPIO gpio)
add_subdirectory_ifdef(CONFIG_INPUT input)
