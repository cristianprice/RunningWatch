#include "settings_screen.h"

#include <stdio.h>
#include <stdlib.h>
#include <time.h>

#include "esp_err.h"
#include "esp_log.h"
#include "main_screen.h"
#include "theme.h"
#include "watch_rtc.h"
#include "watch_timer_config.h"

static const char *TAG = "settings_screen";

enum
{
    /** Inset keeping the rows clear of the panel's rounded corners. */
    SCREEN_PADDING = 24,
    /** Side of the square +/- stepper buttons. */
    STEP_BUTTON_SIZE = 56,
    /** Side of the round confirm/back buttons. */
    ACTION_BUTTON_SIZE = 96,
    BUTTON_BORDER_WIDTH = 3,
    /** Width reserved for a value, enough for the four digits of a year. */
    VALUE_WIDTH = 92,
    ROW_GAP = 10,
    SECTION_GAP = 26,
    COLUMN_GAP = 8,
    /** Share of the screen's content area an editable row spans. */
    ROW_WIDTH_PCT = 85,
};

/** Every value the screen can edit, in the order it is shown. */
typedef enum
{
    FIELD_HOUR,
    FIELD_MINUTE,
    FIELD_DAY,
    FIELD_MONTH,
    FIELD_YEAR,
    FIELD_SESSION_MINUTE,
    FIELD_SESSION_SECOND,
    FIELD_BREAK_MINUTE,
    FIELD_BREAK_SECOND,
    FIELD_COUNT,
} field_id_t;

/** One editable value and the label showing it. */
typedef struct
{
    int32_t value;
    int32_t min;
    int32_t max;
    /** Zero-padding width used when the value is printed. */
    int32_t digits;
    lv_obj_t *label;
} field_t;

typedef struct settings_screen_t settings_screen_t;

/** What a single stepper button does, passed to the click handler. */
typedef struct
{
    settings_screen_t *screen;
    field_id_t field;
    int32_t delta;
} step_t;

struct settings_screen_t
{
    field_t fields[FIELD_COUNT];
    /** One entry per stepper button: two (down, up) for each field. */
    step_t steps[FIELD_COUNT * 2];
};

/** Number of days February has in @p year. */
static int32_t days_in_month(int32_t month, int32_t year)
{
    static const int32_t lengths[] = {31, 28, 31, 30, 31, 30, 31, 31, 30, 31, 30, 31};

    if (month == 2 && ((year % 4 == 0 && year % 100 != 0) || year % 400 == 0)) {
        return 29;
    }
    return lengths[month - 1];
}

/** Re-print a field's label from its current value. */
static void field_refresh(field_t *field)
{
    lv_label_set_text_fmt(field->label, "%0*" LV_PRId32, (int)field->digits, field->value);
}

/**
 * Add @p delta to a field, wrapping around at its limits.
 *
 * Wrapping is what makes a two-button stepper usable on a watch: reaching 59
 * from 0 is one tap down rather than fifty-nine taps up.
 */
static void field_step(field_t *field, int32_t delta)
{
    const int32_t span = field->max - field->min + 1;

    field->value += delta;
    if (field->value > field->max) {
        field->value -= span;
    } else if (field->value < field->min) {
        field->value += span;
    }
    field_refresh(field);
}

/**
 * Keep the day field inside the month it belongs to.
 *
 * Called after the month or the year changes, so 31 January never survives a
 * switch to February.
 */
static void clamp_day(settings_screen_t *state)
{
    field_t *day = &state->fields[FIELD_DAY];
    const int32_t last = days_in_month(
        state->fields[FIELD_MONTH].value,
        state->fields[FIELD_YEAR].value);

    day->max = last;
    if (day->value > last) {
        day->value = last;
        field_refresh(day);
    }
}

static void on_step_clicked(lv_event_t *event)
{
    const step_t *step = lv_event_get_user_data(event);

    field_step(&step->screen->fields[step->field], step->delta);

    if (step->field == FIELD_MONTH || step->field == FIELD_YEAR) {
        clamp_day(step->screen);
    }
}

/** Free the screen state once the screen itself is gone. */
static void on_screen_deleted(lv_event_t *event)
{
    free(lv_event_get_user_data(event));
}

/** Leave the settings screen, dropping it in favour of a fresh main screen. */
static void show_main_screen(void)
{
    lv_screen_load_anim(main_screen_create(), LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

static void on_back_clicked(lv_event_t *event)
{
    (void)event;
    show_main_screen();
}

/**
 * Apply every edit: the wall clock to the RTC, the durations to NVS.
 *
 * The seconds of the wall clock are deliberately zeroed — the user sets a time
 * to the minute, and starting the minute exactly is more useful than keeping
 * whatever second the screen happened to be opened at.
 */
static void on_save_clicked(lv_event_t *event)
{
    settings_screen_t *state = lv_event_get_user_data(event);

    struct tm local = {
        .tm_year = state->fields[FIELD_YEAR].value - 1900,
        .tm_mon = state->fields[FIELD_MONTH].value - 1,
        .tm_mday = state->fields[FIELD_DAY].value,
        .tm_hour = state->fields[FIELD_HOUR].value,
        .tm_min = state->fields[FIELD_MINUTE].value,
        .tm_sec = 0,
        .tm_isdst = -1,
    };

    const time_t utc = mktime(&local);
    if (utc == (time_t)-1) {
        ESP_LOGE(TAG, "Refusing to set an invalid date");
    } else {
        ESP_ERROR_CHECK_WITHOUT_ABORT(watch_rtc_set_time(utc));
    }

    const watch_timer_config_t config = {
        .session_seconds = (uint16_t)(state->fields[FIELD_SESSION_MINUTE].value * 60
                                      + state->fields[FIELD_SESSION_SECOND].value),
        .break_seconds = (uint16_t)(state->fields[FIELD_BREAK_MINUTE].value * 60
                                    + state->fields[FIELD_BREAK_SECOND].value),
    };
    ESP_ERROR_CHECK_WITHOUT_ABORT(watch_timer_config_set(config));

    show_main_screen();
}

/** Create a transparent, self-sizing flex container with no decoration. */
static lv_obj_t *create_group(lv_obj_t *parent, lv_flex_flow_t flow, int32_t gap)
{
    lv_obj_t *group = lv_obj_create(parent);
    lv_obj_remove_flag(group, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_opa(group, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(group, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(group, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(group, gap, LV_PART_MAIN);
    lv_obj_set_style_pad_column(group, gap, LV_PART_MAIN);
    lv_obj_set_flex_flow(group, flow);
    lv_obj_set_flex_align(
        group,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_size(group, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    return group;
}

/** Shared look of every button on this screen: outline only, orange ring. */
static lv_obj_t *create_outline_button(lv_obj_t *parent, const char *icon, int32_t size)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, size, size);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, BUTTON_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, THEME_COLOR_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, THEME_COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, THEME_COLOR_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, THEME_COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
    lv_obj_center(label);

    return button;
}

/** Add a section title above the rows it introduces. */
static void create_section_title(lv_obj_t *parent, const char *text)
{
    lv_obj_t *label = lv_label_create(parent);
    lv_label_set_text(label, text);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, THEME_COLOR_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_pad_top(label, SECTION_GAP, LV_PART_MAIN);
}

/**
 * Build one editable row: a caption on the left, then "- value +".
 *
 * Both stepper buttons also react to LV_EVENT_LONG_PRESSED_REPEAT, so holding
 * one runs through a range instead of asking for a tap per unit. Pairing that
 * with LV_EVENT_SHORT_CLICKED rather than LV_EVENT_CLICKED keeps the release
 * at the end of a hold from adding one extra step.
 */
static void create_field_row(
    lv_obj_t *parent,
    settings_screen_t *state,
    field_id_t id,
    const char *caption,
    int32_t min,
    int32_t max,
    int32_t digits,
    int32_t initial)
{
    field_t *field = &state->fields[id];
    field->min = min;
    field->max = max;
    field->digits = digits;
    field->value = initial < min ? min : (initial > max ? max : initial);

    lv_obj_t *row = create_group(parent, LV_FLEX_FLOW_ROW, COLUMN_GAP);
    lv_obj_set_width(row, lv_pct(ROW_WIDTH_PCT));
    lv_obj_set_flex_align(
        row,
        LV_FLEX_ALIGN_END,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);

    lv_obj_t *name = lv_label_create(row);
    lv_label_set_text(name, caption);
    lv_label_set_long_mode(name, LV_LABEL_LONG_MODE_CLIP);
    lv_obj_set_style_text_font(name, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(name, THEME_COLOR_ON_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_flex_grow(name, 1);

    step_t *down = &state->steps[id * 2];
    *down = (step_t){.screen = state, .field = id, .delta = -1};
    lv_obj_t *minus = create_outline_button(row, LV_SYMBOL_MINUS, STEP_BUTTON_SIZE);
    lv_obj_add_event_cb(minus, on_step_clicked, LV_EVENT_SHORT_CLICKED, down);
    lv_obj_add_event_cb(minus, on_step_clicked, LV_EVENT_LONG_PRESSED_REPEAT, down);

    field->label = lv_label_create(row);
    lv_obj_set_width(field->label, VALUE_WIDTH);
    lv_obj_set_style_text_font(field->label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(field->label, THEME_COLOR_ON_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_text_align(field->label, LV_TEXT_ALIGN_CENTER, LV_PART_MAIN);
    field_refresh(field);

    step_t *up = &state->steps[id * 2 + 1];
    *up = (step_t){.screen = state, .field = id, .delta = 1};
    lv_obj_t *plus = create_outline_button(row, LV_SYMBOL_PLUS, STEP_BUTTON_SIZE);
    lv_obj_add_event_cb(plus, on_step_clicked, LV_EVENT_SHORT_CLICKED, up);
    lv_obj_add_event_cb(plus, on_step_clicked, LV_EVENT_LONG_PRESSED_REPEAT, up);
}

/**
 * Build the settings screen (see settings_screen.h).
 *
 * The rows do not fit on a 410 x 502 panel, so the screen itself is the scroll
 * container: a flex column of section titles and stepper rows, with the
 * back/confirm pair at the bottom. State for every field lives in one
 * allocation hung off the screen's user data and released with it, which is
 * what lets the stepper handlers be plain functions over a field index.
 */
lv_obj_t *settings_screen_create(void)
{
    settings_screen_t *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        ESP_LOGE(TAG, "Out of memory");
        return main_screen_create();
    }

    time_t now = time(NULL);
    struct tm local;
    localtime_r(&now, &local);

    const watch_timer_config_t config = watch_timer_config_get();

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_set_style_bg_color(screen, THEME_COLOR_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, THEME_COLOR_ON_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_pad_all(screen, SCREEN_PADDING, LV_PART_MAIN);
    lv_obj_set_style_pad_row(screen, ROW_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(screen, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        screen,
        LV_FLEX_ALIGN_START,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_set_scroll_dir(screen, LV_DIR_VER);
    lv_obj_set_user_data(screen, state);
    lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, state);

    create_section_title(screen, "Time");
    create_field_row(screen, state, FIELD_HOUR, "Hr", 0, 23, 2, local.tm_hour);
    create_field_row(screen, state, FIELD_MINUTE, "Min", 0, 59, 2, local.tm_min);
    create_field_row(screen, state, FIELD_YEAR, "Yr", 2024, 2099, 4, local.tm_year + 1900);
    create_field_row(screen, state, FIELD_MONTH, "Mon", 1, 12, 2, local.tm_mon + 1);
    create_field_row(screen, state, FIELD_DAY, "Day", 1, 31, 2, local.tm_mday);
    clamp_day(state);

    create_section_title(screen, "Session");
    create_field_row(
        screen, state, FIELD_SESSION_MINUTE, "Min", 0, 99, 2, config.session_seconds / 60);
    create_field_row(
        screen, state, FIELD_SESSION_SECOND, "Sec", 0, 59, 2, config.session_seconds % 60);

    create_section_title(screen, "Break");
    create_field_row(
        screen, state, FIELD_BREAK_MINUTE, "Min", 0, 99, 2, config.break_seconds / 60);
    create_field_row(
        screen, state, FIELD_BREAK_SECOND, "Sec", 0, 59, 2, config.break_seconds % 60);

    lv_obj_t *actions = create_group(screen, LV_FLEX_FLOW_ROW, SECTION_GAP);
    lv_obj_set_style_pad_top(actions, SECTION_GAP, LV_PART_MAIN);

    lv_obj_t *back = create_outline_button(actions, LV_SYMBOL_CLOSE, ACTION_BUTTON_SIZE);
    lv_obj_set_user_data(back, (void *)"discard_settings");
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *save = create_outline_button(actions, LV_SYMBOL_OK, ACTION_BUTTON_SIZE);
    lv_obj_set_user_data(save, (void *)"save_settings");
    lv_obj_add_event_cb(save, on_save_clicked, LV_EVENT_CLICKED, state);

    return screen;
}
