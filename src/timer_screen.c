#include "timer_screen.h"

#include <stdio.h>
#include <stdlib.h>

#include "esp_err.h"
#include "battery_widget.h"
#include "main_screen.h"
#include "theme.h"
#include "watch_audio.h"
#include "watch_timer_config.h"

enum
{
    /** Side of the round, icon-only back button. */
    BACK_BUTTON_SIZE = 110,
    BUTTON_BORDER_WIDTH = 4,
    /** Gap between the phase caption and the countdown. */
    PHASE_GAP = 8,
    /** Gap between the countdown block and the back button. */
    CONTENT_GAP = 56,
    /** Inset of the battery icon, clear of the panel's rounded corner. */
    BATTERY_MARGIN = 36,
    /** Countdown refresh: fast enough that a second is never shown late. */
    TICK_PERIOD_MS = 100,
    /** Strikes sounded when a phase runs out. */
    PHASE_END_GONGS = 3,
    /** Volume the cues are played at, loud enough to be heard while moving. */
    GONG_VOLUME = 100,
};

/** The two halves of the interval, in the order they are run. */
typedef enum
{
    PHASE_BREAK,
    PHASE_SESSION,
    PHASE_COUNT,
} phase_t;

/** Everything the countdown needs, hung off the screen's user data. */
typedef struct
{
    lv_obj_t *phase_label;
    lv_obj_t *time_label;
    lv_timer_t *tick;
    /** Duration of each phase in milliseconds, indexed by #phase_t. */
    uint32_t duration_ms[PHASE_COUNT];
    /** LVGL tick the running phase started at. */
    uint32_t started;
    phase_t phase;
    /** Last second written to the label, so it is only relabelled on change. */
    int32_t shown_seconds;
} timer_screen_t;

static const char *const PHASE_NAMES[PHASE_COUNT] = {
    [PHASE_BREAK] = "Break",
    [PHASE_SESSION] = "Session",
};

/** Write @p seconds to the countdown label as MM:SS. */
static void show_seconds(timer_screen_t *state, int32_t seconds)
{
    if (seconds == state->shown_seconds) {
        return;
    }
    state->shown_seconds = seconds;

    char text[8];
    snprintf(text, sizeof(text), "%02d:%02d", (int)(seconds / 60), (int)(seconds % 60));
    lv_label_set_text(state->time_label, text);
}

/** Restart the countdown on @p phase and show its full duration straight away. */
static void start_phase(timer_screen_t *state, phase_t phase)
{
    state->phase = phase;
    state->started = lv_tick_get();
    state->shown_seconds = -1;

    lv_label_set_text(state->phase_label, PHASE_NAMES[phase]);
    show_seconds(state, (int32_t)(state->duration_ms[phase] / 1000));
}

/**
 * Advance the countdown.
 *
 * The remaining time is derived from the tick the phase started at rather than
 * accumulated per callback, so a late or coalesced LVGL timer cannot make the
 * interval drift. The displayed value is rounded up, which is what keeps the
 * full duration on screen for the first second and 00:00 only at the very end.
 */
static void on_tick(lv_timer_t *timer)
{
    timer_screen_t *state = lv_timer_get_user_data(timer);
    const uint32_t elapsed = lv_tick_elaps(state->started);
    const uint32_t duration = state->duration_ms[state->phase];

    if (elapsed >= duration) {
        show_seconds(state, 0);
        watch_audio_play_gongs(PHASE_END_GONGS);
        start_phase(state, state->phase == PHASE_BREAK ? PHASE_SESSION : PHASE_BREAK);
        return;
    }

    show_seconds(state, (int32_t)((duration - elapsed + 999) / 1000));
}

/** Tear the countdown timer down and free the state with the screen. */
static void on_screen_deleted(lv_event_t *event)
{
    timer_screen_t *state = lv_event_get_user_data(event);

    if (state->tick != NULL) {
        lv_timer_delete(state->tick);
    }
    free(state);
}

/** Leave the timer, dropping it in favour of a fresh main screen. */
static void on_back_clicked(lv_event_t *event)
{
    (void)event;
    lv_screen_load_anim(main_screen_create(), LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

/** Round, outline-only, icon-only button — the same look as the main screen. */
static lv_obj_t *create_icon_button(lv_obj_t *parent, const char *icon, int32_t size)
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

/**
 * Build the timer screen (see timer_screen.h).
 *
 * Layout mirrors the main screen: a transparent flex column centered on the
 * screen holds the phase caption over the countdown, with the back button
 * underneath; the battery icon sits outside it in the top-right corner.
 */
lv_obj_t *timer_screen_create(void)
{
    const watch_timer_config_t config = watch_timer_config_get();

    timer_screen_t *state = calloc(1, sizeof(*state));
    if (state == NULL) {
        return main_screen_create();
    }

    state->duration_ms[PHASE_BREAK] = (uint32_t)config.break_seconds * 1000u;
    state->duration_ms[PHASE_SESSION] = (uint32_t)config.session_seconds * 1000u;

    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, THEME_COLOR_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, THEME_COLOR_ON_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_user_data(screen, state);
    lv_obj_add_event_cb(screen, on_screen_deleted, LV_EVENT_DELETE, state);

    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, CONTENT_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        content,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_center(content);

    lv_obj_t *countdown = lv_obj_create(content);
    lv_obj_remove_flag(countdown, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(countdown, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(countdown, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(countdown, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(countdown, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(countdown, PHASE_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(countdown, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        countdown,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);

    state->phase_label = lv_label_create(countdown);
    lv_obj_set_style_text_font(state->phase_label, &lv_font_montserrat_28, LV_PART_MAIN);
    lv_obj_set_style_text_color(state->phase_label, THEME_COLOR_PRIMARY, LV_PART_MAIN);

    state->time_label = lv_label_create(countdown);
    lv_obj_set_style_text_font(state->time_label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(
        state->time_label, THEME_COLOR_ON_BACKGROUND, LV_PART_MAIN);

    lv_obj_t *back = create_icon_button(content, LV_SYMBOL_CLOSE, BACK_BUTTON_SIZE);
    lv_obj_set_user_data(back, (void *)"stop_timer");
    lv_obj_add_event_cb(back, on_back_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *battery = battery_widget_create(screen);
    lv_obj_align(battery, LV_ALIGN_TOP_RIGHT, -BATTERY_MARGIN, BATTERY_MARGIN);

    watch_audio_set_volume(GONG_VOLUME);

    /* The break always runs first, so a session is never entered cold. */
    start_phase(state, PHASE_BREAK);
    state->tick = lv_timer_create(on_tick, TICK_PERIOD_MS, state);

    return screen;
}
