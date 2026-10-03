#include "esp_err.h"
#include "esp_log.h"
#include "lvgl.h"
#include "bsp/display.h"
#include "bsp/esp-bsp.h"
#include "main_screen.h"
#include "watch_audio.h"
#include "watch_power.h"
#include "watch_rtc.h"
#include "watch_sleep.h"
#include "watch_timer_config.h"
#include "theme.h"

static const char *TAG = "running_watch";

void app_main(void)
{
    ESP_LOGI(TAG, "Starting Waveshare AMOLED display and LVGL");
    bsp_display_start();

    ESP_ERROR_CHECK_WITHOUT_ABORT(watch_rtc_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(watch_power_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(watch_audio_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(watch_sleep_init());
    ESP_ERROR_CHECK_WITHOUT_ABORT(watch_timer_config_init());

    if (!bsp_display_lock(0)) {
        ESP_LOGE(TAG, "Failed to lock the LVGL display");
        return;
    }

    theme_apply();
    lv_screen_load(main_screen_create());

    bsp_display_unlock();
    ESP_LOGI(TAG, "LVGL interface ready");
}