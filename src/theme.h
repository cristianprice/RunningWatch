#pragma once

#include "lvgl.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Page/background black the AMOLED panel can switch pixels fully off with. */
#define THEME_COLOR_BACKGROUND lv_color_hex(0x000000)

/** Primary orange used for interactive surfaces. */
#define THEME_COLOR_PRIMARY lv_color_hex(0xFF7A00)

/** Darker orange for pressed/secondary surfaces. */
#define THEME_COLOR_PRIMARY_DARK lv_color_hex(0xB34D00)

/** Text/icon colour drawn on top of the black background. */
#define THEME_COLOR_ON_BACKGROUND lv_color_hex(0xFFA94D)

/**
 * Install the black/orange theme on the active display.
 *
 * Must be called with the LVGL lock held, after bsp_display_start() and before
 * the first screen is created so every object inherits the theme.
 */
void theme_apply(void);

#ifdef __cplusplus
}
#endif
