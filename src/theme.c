#include "theme.h"

/**
 * Build the black/orange theme (see theme.h).
 *
 * The stock LVGL default theme in dark mode already gives black-ish surfaces;
 * feeding it the orange palette as primary/secondary colour tints every widget
 * the UI uses, so screens only need to set their own background.
 */
void theme_apply(void)
{
    lv_display_t *display = lv_display_get_default();
    if (display == NULL) {
        return;
    }

    lv_theme_t *theme = lv_theme_default_init(
        display,
        THEME_COLOR_PRIMARY,
        THEME_COLOR_PRIMARY_DARK,
        true,
        lv_font_get_default()
    );
    lv_display_set_theme(display, theme);
}
