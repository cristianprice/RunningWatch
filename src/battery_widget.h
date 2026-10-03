#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/**
 * Create the icon-only battery indicator.
 *
 * The widget is a single label showing the LV_SYMBOL_BATTERY_* glyph that
 * matches the current state of charge, or LV_SYMBOL_CHARGE while the watch is
 * on the charger. No percentage text is drawn. It polls watch_power.c on an
 * LVGL timer that is deleted together with the widget.
 *
 * The label is not positioned; the caller aligns it inside its parent.
 *
 * Must be called with the LVGL lock held.
 *
 * @param parent object the indicator is added to.
 * @return the indicator label.
 */
lv_obj_t *battery_widget_create(lv_obj_t *parent);

#ifdef __cplusplus
}
#endif
