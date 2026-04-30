# Copyright (c) 2026 badjeff
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_library_property(ALLOW_EMPTY TRUE)

zephyr_library_sources_ifdef(CONFIG_GPIO_ADS1220		gpio_ads1220.c)

zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
