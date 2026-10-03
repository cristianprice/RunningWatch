#include "battery_widget.h"
#include "theme.h"
#include "watch_power.h"

enum {
    /** The gauge moves slowly, so polling the PMU this often is plenty. */
    BATTERY_REFRESH_PERIOD_MS = 10000,

    /** Upper bound of each battery glyph, from empty to full. */
    BATTERY_LEVEL_EMPTY = 10,
    BATTERY_LEVEL_LOW = 35,
    BATTERY_LEVEL_HALF = 60,
    BATTERY_LEVEL_HIGH = 85,
};

/**
 * Pick the glyph standing for a state of charge.
 *
 * LVGL ships five battery symbols, so the percentage is bucketed into the
 * thresholds above rather than drawn as a number.
 */
static const char *battery_symbol(int percent)
{
    if (percent <= BATTERY_LEVEL_EMPTY) {
        return LV_SYMBOL_BATTERY_EMPTY;
    }
    if (percent <= BATTERY_LEVEL_LOW) {
        return LV_SYMBOL_BATTERY_1;
    }
    if (percent <= BATTERY_LEVEL_HALF) {
        return LV_SYMBOL_BATTERY_2;
    }
    if (percent <= BATTERY_LEVEL_HIGH) {
        return LV_SYMBOL_BATTERY_3;
    }

    return LV_SYMBOL_BATTERY_FULL;
}

static void battery_widget_refresh(lv_obj_t *label)
{
    watch_power_battery_t battery;

    if (watch_power_get_battery(&battery) != ESP_OK || !battery.present) {
        lv_label_set_text(label, LV_SYMBOL_BATTERY_EMPTY);
        lv_obj_set_style_text_color(label, THEME_COLOR_PRIMARY_DARK, LV_PART_MAIN);
        return;
    }

    if (battery.charging) {
        lv_label_set_text(label, LV_SYMBOL_CHARGE);
        lv_obj_set_style_text_color(label, THEME_COLOR_PRIMARY, LV_PART_MAIN);
        return;
    }

    /* A gauge that has not settled yet reads -1; show it as empty, dimmed. */
    const bool known = battery.percent >= 0;
    lv_label_set_text(label, battery_symbol(known ? battery.percent : 0));
    lv_obj_set_style_text_color(
        label,
        known ? THEME_COLOR_ON_BACKGROUND : THEME_COLOR_PRIMARY_DARK,
        LV_PART_MAIN
    );
}

static void battery_widget_tick(lv_timer_t *timer)
{
    battery_widget_refresh((lv_obj_t *)lv_timer_get_user_data(timer));
}

static void battery_widget_delete(lv_event_t *event)
{
    lv_timer_delete((lv_timer_t *)lv_event_get_user_data(event));
}

/**
 * Build the battery indicator (see battery_widget.h).
 */
lv_obj_t *battery_widget_create(lv_obj_t *parent)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, LV_PART_MAIN);

    battery_widget_refresh(label);

    lv_timer_t *timer =
        lv_timer_create(battery_widget_tick, BATTERY_REFRESH_PERIOD_MS, label);
    lv_obj_add_event_cb(label, battery_widget_delete, LV_EVENT_DELETE, timer);

    return label;
}
