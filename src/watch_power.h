#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Snapshot of the AXP2101 fuel gauge. */
typedef struct {
    /** True when a battery is attached; the rest is meaningless otherwise. */
    bool present;
    /** True while the charger is actively pushing current into the cell. */
    bool charging;
    /** State of charge in percent, or -1 when the gauge has no reading yet. */
    int percent;
} watch_power_battery_t;

/**
 * Bind the on-board AXP2101 PMU and switch its battery metering on.
 *
 * Enables battery detection and the battery-voltage ADC, which the built-in
 * fuel gauge needs before #watch_power_get_battery returns a usable value.
 *
 * Safe to call before the display is started; it does not touch LVGL.
 *
 * @return ESP_OK on success, an esp_err_t from the I2C layer otherwise.
 */
esp_err_t watch_power_init(void);

/**
 * Read the current battery state.
 *
 * @param out receives presence, charging state and state of charge.
 * @return ESP_OK on success, an esp_err_t from the I2C layer otherwise.
 */
esp_err_t watch_power_get_battery(watch_power_battery_t *out);

/**
 * Read one AXP2101 register.
 *
 * This module owns the only I2C handle for the PMU, so other modules that need
 * one of its features — the PWR button IRQ, for instance — go through here
 * instead of attaching the chip a second time.
 *
 * @param reg register address.
 * @param value receives the register contents.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE before #watch_power_init.
 */
esp_err_t watch_power_read_register(uint8_t reg, uint8_t *value);

/**
 * Write one AXP2101 register.
 *
 * @param reg register address.
 * @param value value to store.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE before #watch_power_init.
 */
esp_err_t watch_power_write_register(uint8_t reg, uint8_t value);

#ifdef __cplusplus
}
#endif
