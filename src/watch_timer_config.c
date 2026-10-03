#include "watch_timer_config.h"

#include "esp_log.h"
#include "nvs.h"
#include "nvs_flash.h"

static const char *TAG = "watch_timer_config";

/** NVS namespace and keys holding the interval durations. */
#define CONFIG_NAMESPACE "runwatch"
#define KEY_SESSION "session_s"
#define KEY_BREAK "break_s"

/** Durations a watch that has never been configured starts with. */
enum
{
    DEFAULT_SESSION_SECONDS = 5 * 60,
    DEFAULT_BREAK_SECONDS = 60,
};

static watch_timer_config_t s_config = {
    .session_seconds = DEFAULT_SESSION_SECONDS,
    .break_seconds = DEFAULT_BREAK_SECONDS,
};

/** Keep a duration inside the range the editor and the timer can handle. */
static uint16_t clamp_seconds(uint16_t seconds)
{
    if (seconds < WATCH_TIMER_MIN_SECONDS) {
        return WATCH_TIMER_MIN_SECONDS;
    }
    if (seconds > WATCH_TIMER_MAX_SECONDS) {
        return WATCH_TIMER_MAX_SECONDS;
    }
    return seconds;
}

/**
 * Initialize the NVS flash partition.
 *
 * A partition left over from an older firmware (different layout, or simply
 * full) is erased and re-created rather than failing the boot: the only thing
 * stored there is the timer configuration, which falls back to its defaults.
 */
static esp_err_t nvs_ready(void)
{
    esp_err_t err = nvs_flash_init();
    if (err == ESP_ERR_NVS_NO_FREE_PAGES || err == ESP_ERR_NVS_NEW_VERSION_FOUND) {
        ESP_LOGW(TAG, "Re-initializing the NVS partition (%s)", esp_err_to_name(err));
        ESP_ERROR_CHECK(nvs_flash_erase());
        err = nvs_flash_init();
    }
    return err;
}

esp_err_t watch_timer_config_init(void)
{
    esp_err_t err = nvs_ready();
    if (err != ESP_OK) {
        return err;
    }

    nvs_handle_t handle;
    err = nvs_open(CONFIG_NAMESPACE, NVS_READONLY, &handle);
    if (err == ESP_ERR_NVS_NOT_FOUND) {
        ESP_LOGI(TAG, "No saved timer configuration, using the defaults");
        return ESP_OK;
    }
    if (err != ESP_OK) {
        return err;
    }

    uint16_t value = 0;
    if (nvs_get_u16(handle, KEY_SESSION, &value) == ESP_OK) {
        s_config.session_seconds = clamp_seconds(value);
    }
    if (nvs_get_u16(handle, KEY_BREAK, &value) == ESP_OK) {
        s_config.break_seconds = clamp_seconds(value);
    }
    nvs_close(handle);

    ESP_LOGI(
        TAG,
        "Timer configuration: session %u s, break %u s",
        s_config.session_seconds,
        s_config.break_seconds);
    return ESP_OK;
}

watch_timer_config_t watch_timer_config_get(void)
{
    return s_config;
}

esp_err_t watch_timer_config_set(watch_timer_config_t config)
{
    s_config.session_seconds = clamp_seconds(config.session_seconds);
    s_config.break_seconds = clamp_seconds(config.break_seconds);

    nvs_handle_t handle;
    esp_err_t err = nvs_open(CONFIG_NAMESPACE, NVS_READWRITE, &handle);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot open NVS for writing: %s", esp_err_to_name(err));
        return err;
    }

    err = nvs_set_u16(handle, KEY_SESSION, s_config.session_seconds);
    if (err == ESP_OK) {
        err = nvs_set_u16(handle, KEY_BREAK, s_config.break_seconds);
    }
    if (err == ESP_OK) {
        err = nvs_commit(handle);
    }
    nvs_close(handle);

    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Cannot save the timer configuration: %s", esp_err_to_name(err));
    }
    return err;
}
