#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create the clock block: a large HH:MM:SS readout with the date underneath.
 *
 * The widget is a transparent, self-sizing flex column that any screen can drop
 * into its own layout. It refreshes itself from the system clock (which
 * watch_rtc.c keeps in step with the PCF85063) through an LVGL timer that is
 * deleted together with the widget.
 *
 * Must be called with the LVGL lock held.
 *
 * @param parent object the clock is added to.
 * @return the clock container.
 */
lv_obj_t *clock_widget_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
