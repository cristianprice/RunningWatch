#pragma once

#include <stdint.h>
#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest session or break the editor can express (99:59). */
#define WATCH_TIMER_MAX_SECONDS 5999

/** Shortest session or break that still makes sense. */
#define WATCH_TIMER_MIN_SECONDS 1

/** Session/break durations of the interval timer, in seconds. */
typedef struct
{
    uint16_t session_seconds;
    uint16_t break_seconds;
} watch_timer_config_t;

/**
 * Bring up the persistent store and load the saved interval durations.
 *
 * The values live in NVS rather than on the RTC: the PCF85063 exposes a single
 * byte of user RAM, which cannot hold two durations, while NVS survives a
 * power-off just as well.
 *
 * A missing or corrupt store is not an error — the built-in defaults are used
 * and written back on the next save.
 *
 * Safe to call before the display is started; it does not touch LVGL.
 *
 * @return ESP_OK on success, an esp_err_t from NVS otherwise.
 */
esp_err_t watch_timer_config_init(void);

/**
 * Read the durations currently in force.
 *
 * Returns the defaults when watch_timer_config_init() has not run yet, so the
 * caller never has to handle an unset configuration.
 *
 * @return the active configuration by value.
 */
watch_timer_config_t watch_timer_config_get(void);

/**
 * Store new durations and persist them.
 *
 * Both values are clamped to #WATCH_TIMER_MIN_SECONDS …
 * #WATCH_TIMER_MAX_SECONDS before being written.
 *
 * @param config durations to save.
 * @return ESP_OK on success, an esp_err_t from NVS otherwise. The in-memory
 *         copy is updated even when the write fails, so the UI stays
 *         consistent with what the user asked for.
 */
esp_err_t watch_timer_config_set(watch_timer_config_t config);

#ifdef __cplusplus
}
#endif
