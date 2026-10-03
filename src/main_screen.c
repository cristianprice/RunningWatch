#include "main_screen.h"
#include "battery_widget.h"
#include "clock_widget.h"
#include "settings_screen.h"
#include "theme.h"
#include "timer_screen.h"

enum
{
    BUTTON_SIZE = 150,
    BUTTON_GAP = 28,
    BUTTON_BORDER_WIDTH = 4,
    CLOCK_GAP = 48,
    /** Inset of the battery icon, clear of the panel's rounded corner. */
    BATTERY_MARGIN = 36,
};

/**
 * Create one round, icon-only button.
 *
 * The button is outline-only: a transparent fill with an orange ring, and the
 * icon drawn in the same orange. The icon is a centered label holding an
 * LV_SYMBOL_* glyph, so no text is shown besides the symbol itself.
 *
 * @param parent object the button is added to.
 * @param icon   LV_SYMBOL_* string drawn inside the button.
 * @return the new button object.
 */
static lv_obj_t *create_icon_button(lv_obj_t *parent, const char *icon)
{
    lv_obj_t *button = lv_button_create(parent);
    lv_obj_set_size(button, BUTTON_SIZE, BUTTON_SIZE);
    lv_obj_set_style_radius(button, LV_RADIUS_CIRCLE, LV_PART_MAIN);
    lv_obj_set_style_bg_opa(button, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(button, BUTTON_BORDER_WIDTH, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, THEME_COLOR_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_border_opa(button, LV_OPA_COVER, LV_PART_MAIN);
    lv_obj_set_style_border_color(button, THEME_COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
    lv_obj_set_style_shadow_width(button, 0, LV_PART_MAIN);

    lv_obj_t *label = lv_label_create(button);
    lv_label_set_text(label, icon);
    lv_obj_set_style_text_font(label, &lv_font_montserrat_48, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, THEME_COLOR_PRIMARY, LV_PART_MAIN);
    lv_obj_set_style_text_color(label, THEME_COLOR_PRIMARY_DARK, LV_STATE_PRESSED);
    lv_obj_center(label);

    return button;
}

/**
 * Open the settings screen when the gear button is tapped.
 *
 * The main screen is handed to lv_screen_load_anim()'s auto-delete, so it is
 * rebuilt from scratch on the way back and always reflects a time the user may
 * have just changed.
 */
static void on_program_clicked(lv_event_t *event)
{
    (void)event;
    lv_screen_load_anim(settings_screen_create(), LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * Start the interval timer when the play button is tapped.
 *
 * The main screen is auto-deleted by the load, so its clock and battery timers
 * stop while the countdown runs, and coming back rebuilds it from scratch.
 */
static void on_start_clicked(lv_event_t *event)
{
    (void)event;
    lv_screen_load_anim(timer_screen_create(), LV_SCR_LOAD_ANIM_NONE, 0, 0, true);
}

/**
 * Build the main screen (see main_screen.h).
 *
 * Layout: a transparent flex column is centered on the screen and holds the
 * clock block on top of a transparent flex row with the two buttons, which
 * keeps the whole group centered regardless of the widget sizes. The battery
 * icon sits outside that column, pinned to the top-right corner.
 * Each button carries a user-data tag identifying its action.
 */
lv_obj_t *main_screen_create(void)
{
    lv_obj_t *screen = lv_obj_create(NULL);
    lv_obj_remove_flag(screen, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_style_bg_color(screen, THEME_COLOR_BACKGROUND, LV_PART_MAIN);
    lv_obj_set_style_text_color(screen, THEME_COLOR_ON_BACKGROUND, LV_PART_MAIN);

    lv_obj_t *content = lv_obj_create(screen);
    lv_obj_remove_flag(content, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(content, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(content, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(content, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_row(content, CLOCK_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(content, LV_FLEX_FLOW_COLUMN);
    lv_obj_set_flex_align(
        content,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);
    lv_obj_center(content);

    clock_widget_create(content);

    lv_obj_t *button_row = lv_obj_create(content);
    lv_obj_remove_flag(button_row, LV_OBJ_FLAG_SCROLLABLE);
    lv_obj_set_size(button_row, LV_SIZE_CONTENT, LV_SIZE_CONTENT);
    lv_obj_set_style_bg_opa(button_row, LV_OPA_TRANSP, LV_PART_MAIN);
    lv_obj_set_style_border_width(button_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_all(button_row, 0, LV_PART_MAIN);
    lv_obj_set_style_pad_column(button_row, BUTTON_GAP, LV_PART_MAIN);
    lv_obj_set_flex_flow(button_row, LV_FLEX_FLOW_ROW);
    lv_obj_set_flex_align(
        button_row,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER,
        LV_FLEX_ALIGN_CENTER);

    lv_obj_t *program_button = create_icon_button(button_row, LV_SYMBOL_SETTINGS);
    lv_obj_set_user_data(program_button, (void *)"program_timer");
    lv_obj_add_event_cb(program_button, on_program_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *start_button = create_icon_button(button_row, LV_SYMBOL_PLAY);
    lv_obj_set_user_data(start_button, (void *)"start_timer");
    lv_obj_add_event_cb(start_button, on_start_clicked, LV_EVENT_CLICKED, NULL);

    lv_obj_t *battery = battery_widget_create(screen);
    lv_obj_align(battery, LV_ALIGN_TOP_RIGHT, -BATTERY_MARGIN, BATTERY_MARGIN);

    return screen;
}
