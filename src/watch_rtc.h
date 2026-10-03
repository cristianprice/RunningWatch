#pragma once

#include <time.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * POSIX TZ string the system clock runs in.
 *
 * The PCF85063 always holds **UTC**; everything the user sees goes through
 * localtime_r(), so changing the watch's timezone means editing this string
 * only.
 */
#define WATCH_RTC_TIMEZONE "EET-2EEST,M3.5.0/3,M10.5.0/4"

/**
 * Bind the on-board PCF85063 RTC and seed the system clock from it.
 *
 * Installs #WATCH_RTC_TIMEZONE, adds the RTC to the BSP I2C bus and copies the
 * backed-up time into the ESP-IDF system clock. If the RTC reports that its
 * oscillator stopped (dead backup battery, first ever power-up) the firmware
 * build timestamp is written to the RTC instead, so the watch always starts
 * from a plausible date.
 *
 * Safe to call before the display is started; it does not touch LVGL.
 *
 * @return ESP_OK on success, an esp_err_t from the I2C layer otherwise.
 */
esp_err_t watch_rtc_init(void);

/**
 * Read the backed-up wall clock straight from the RTC chip.
 *
 * @param out_utc receives the stored time as a UTC epoch value.
 * @return ESP_OK on success, ESP_ERR_INVALID_STATE if the RTC oscillator
 *         stopped and its contents are therefore meaningless.
 */
esp_err_t watch_rtc_get_time(time_t *out_utc);

/**
 * Set the wall clock, updating both the RTC chip and the system clock.
 *
 * This is the only way the time should ever be changed: it keeps the two
 * clocks in step so the new value survives a reboot or a power cycle.
 *
 * @param utc new time as a UTC epoch value.
 * @return ESP_OK on success, an esp_err_t from the I2C layer otherwise.
 */
esp_err_t watch_rtc_set_time(time_t utc);

#ifdef __cplusplus
}
#endif
