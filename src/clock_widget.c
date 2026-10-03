#include <stdbool.h>
#include <time.h>

#include "clock_widget.h"
#include "theme.h"

enum {
    /** Sub-second tick so a rolling seconds digit is never a full second late. */
    CLOCK_REFRESH_PERIOD_MS = 200,
    CLOCK_ROW_GAP = 4,
};

/** Labels a single clock instance refreshes; owned by the container. */
typedef struct {
    lv_obj_t *time_label;
    lv_obj_t *date_label;
    int last_second;
} clock_widget_t;

static void clock_widget_refresh(clock_widget_t *clock, bool force)
{
    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    if (!force && local.tm_sec == clock->last_second) {
        return;
    }
    clock->last_second = local.tm_sec;

    char buffer[32];
    strftime(buffer, sizeof(buffer), "%H:%M:%S", &local);
    lv_label_set_text(clock->time_label, buffer);

    strftime(buffer, sizeof(buffer), "%a %d %b %Y", &local);
    lv_label_set_text(clock->date_label, buffer);
}

static void clock_widget_tick(lv_timer_t *timer)
{
    clock_widget_refresh((clock_widget_t *)lv_timer_get_user_data(timer), false);
}

static void clock_widget_delete(lv_event_t *event)
{
    lv_timer_delete((lv_timer_t *)lv_event_get_user_data(event));
    lv_free(lv_obj_get_user_data(lv_event_get_target_obj(event)));
}

/**
 * Build the clock block (see clock_widget.h).
 *
 * The two labels sit in a transparent flex column so the pair stays centered as
 * a unit inside whatever layout the screen uses. The per-instance state is
 * stored in the container's user data, which LVGL frees with the object.
 */
lv_obj_t *clock_widget_create(lv_obj_t *parent)
{
    lv_obj_t *container = lv_obj_create(parent);
    lv_obj_remove_flag(container, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(container, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(container, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(container, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(container, CLOCK_ROW_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(container, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        container,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER
    );

    clock_widget_t *clock = lv_malloc_zeroed(sizeof(clock_widget_t));
    clock->last_second = -1;

    clock->time_label = lv_label_create(container);
    lv_obj_set_style_text_font(clock->time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(clock->time_label, THEME_COLOR_PRIMARY, LV_PART_MAIN);

    clock->date_label = lv_label_create(container);
    lv_obj_set_style_text_font(clock->date_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        clock->date_label, THEME_COLOR_ON_BACKGROUND, LV_PART_MAIN
    );

    clock_widget_refresh(clock, true);

    lv_obj_set_user_data(container, clock);

    lv_timer_t *timer = lv_timer_create(clock_widget_tick, CLOCK_REFRESH_PERIOD_MS, clock);
    lv_obj_add_event_cb(container, clock_widget_delete, LV_EVENT_DELETE, timer);

    return container;
}
