# Copyright (c) 2026 badjeff
# SPDX-License-Identifier: Apache-2.0

zephyr_library_amend()
zephyr_library_property(ALLOW_EMPTY TRUE)

zephyr_library_sources_ifdef(CONFIG_ADC_ADS1220		adc_ads1220.c)

# add "zephyr/drivers/adc/adc_context.h" to include path
zephyr_include_directories(${ZEPHYR_BASE}/drivers/adc)

# synced "zephyr/drivers/adc/adc_context.h" to local "drivers/adc"
# note: keep this for copying header if the header includes local path
# execute_process(COMMAND pwd WORKING_DIRECTORY ${ZEPHYR_BASE} OUTPUT_VARIABLE PWD_BASE)
# message("pwd at ZEPHYR_BASE: ${PWD_BASE}")
# set(SRC_ADC_CTX_H "${ZEPHYR_BASE}/drivers/adc/adc_context.h")
# set(DST_ADC_CTX_H "${ZEPHYR_CURRENT_MODULE_DIR}/drivers/adc/adc_context.h")
# file(READ "${SRC_ADC_CTX_H}" CONTENTS)
# file(WRITE "${DST_ADC_CTX_H}" "${CONTENTS}")
# message(STATUS "drivers/adc/adc_context.h synced")

zephyr_include_directories(${ZEPHYR_CURRENT_MODULE_DIR}/include)
zephyr_include_directories(${APPLICATION_SOURCE_DIR}/include)
