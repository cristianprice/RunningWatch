#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build the interval timer screen.
 *
 * Counts the configured break down first, sounds three gongs, then counts the
 * session down and sounds them again, looping until the back button is
 * tapped. The durations come from watch_timer_config_get() when the screen is
 * created, so editing them in the settings screen takes effect the next time
 * the timer is started.
 *
 * The screen is returned detached; the caller shows it with lv_screen_load()
 * or lv_screen_load_anim(). Must be called with the LVGL lock held when it
 * runs outside an LVGL event callback.
 *
 * @return the new screen object.
 */
lv_obj_t *timer_screen_create(void);

#ifdef __cplusplus
}
#endif
