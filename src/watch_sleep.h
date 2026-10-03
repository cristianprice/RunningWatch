#pragma once

#include <stdbool.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Start watching the PWR button.
 *
 * A short press puts the display to sleep, the next one wakes it up. Holding
 * the button past the PMU's IRQLEVEL (~1.5 s) powers the watch off through
 * #watch_sleep_power_off.
 *
 * The button is wired to the AXP2101's PWRON pin only — its IRQ line never
 * reaches the ESP32 — so a small task polls the PMU's interrupt status over
 * I2C instead of taking a GPIO interrupt.
 *
 * Must run after bsp_display_start() and watch_power_init().
 *
 * @return ESP_OK on success, an esp_err_t from the I2C layer otherwise.
 */
esp_err_t watch_sleep_init(void);

/**
 * Turn the display off and stop LVGL rendering.
 *
 * Idempotent; safe to call from any task.
 *
 * @return ESP_OK on success or when already asleep.
 */
esp_err_t watch_sleep_enter(void);

/**
 * Resume LVGL rendering and restore the display brightness.
 *
 * Idempotent; safe to call from any task.
 *
 * @return ESP_OK on success or when already awake.
 */
esp_err_t watch_sleep_exit(void);

/** True while the watch is in the sleep state. */
bool watch_sleep_is_asleep(void);

/**
 * Cut power to the whole board through the AXP2101.
 *
 * This is a real power-off, not a suspend: the PMU drops its rails, so the
 * ESP32 loses RAM and cold-boots the next time it comes up. Only the RTC keeps
 * running, on its own backup rail, so the clock is still right afterwards.
 *
 * Holding PWR longer than the PMU's ONLEVEL (128 ms - 2 s) powers the board
 * back on. With USB attached, VBUS alone can re-power the board immediately,
 * so this behaves as intended on battery only.
 *
 * The call does not return when it succeeds.
 *
 * @return an esp_err_t from the I2C layer if the PMU could not be reached.
 */
esp_err_t watch_sleep_power_off(void);

#ifdef __cplusplus
}
#endif
