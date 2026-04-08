# Copyright (c) 2026 badjeff
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_library_property(ALLOW_EMPTY TRUE)

zephyr_library_sources_ifdef(CONFIG_INPUT_ANALOG_AXIS_HIRES input_analog_axis_hires.c)
zephyr_library_sources_ifdef(CONFIG_INPUT_ANALOG_AXIS_HIRES input_analog_axis_hires_settings.c)

zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
