#pragma once

#include "lvgl.h"

/**
 * Build the watch's main screen: a dark background with two centered,
 * icon-only buttons (program the timer, start the timer).
 *
 * The screen is created detached, so the caller is responsible for showing it
 * (e.g. lv_screen_load). Must be called with the LVGL lock held.
 *
 * @return the new screen object.
 */
lv_obj_t *main_screen_create(void);
