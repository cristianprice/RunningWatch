#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Build the settings screen: the wall clock and the interval timer durations,
 * each edited with a pair of stepper buttons.
 *
 * Nothing is applied until the confirm button is tapped; the back button
 * discards every change. Both return to the main screen.
 *
 * The screen is created detached, so the caller is responsible for showing it
 * (e.g. lv_screen_load). Must be called with the LVGL lock held.
 *
 * @return the new screen object.
 */
lv_obj_t *settings_screen_create(void);

#ifdef __cplusplus
}
#endif
