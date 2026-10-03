#include <stdbool.h>
#include <stdint.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "freertos/semphr.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_lvgl_port.h"
#include "bsp/esp-bsp.h"

#include "watch_power.h"
#include "watch_sleep.h"

static const char *TAG = "watch_sleep";

enum {
    /** PMU common configuration; bit 0 is the software power-off trigger. */
    AXP2101_REG_COMMON_CONFIG = 0x10,
    /** IRQ enable bank holding the PWRON (PWR button) sources. */
    AXP2101_REG_IRQ_ENABLE_1 = 0x41,
    /** Matching status bank; its bits are cleared by writing them back. */
    AXP2101_REG_IRQ_STATUS_1 = 0x49,

    /** COMMON_CONFIG bit 0: drop every rail. */
    AXP2101_SOFT_POWEROFF = 0x01,

    /** Bit 3 of the IRQ banks: PWRON short press. */
    AXP2101_IRQ_PWRON_SHORT = 0x08,
    /** Bit 2: PWRON held past IRQLEVEL (1 s - 2.5 s, 1.5 s out of reset). */
    AXP2101_IRQ_PWRON_LONG = 0x04,
    /** Bits 3:0 are the four PWRON events; the rest belong to VBUS/battery. */
    AXP2101_IRQ_PWRON_MASK = 0x0F,

    /**
     * The PMU latches the press, so polling only decides how quickly the
     * screen reacts, not whether the press is seen at all.
     */
    BUTTON_POLL_PERIOD_MS = 50,
    BUTTON_TASK_STACK_SIZE = 3072,
    BUTTON_TASK_PRIORITY = 4,

    /** Grace period for the rails to collapse after a power-off request. */
    POWER_OFF_SETTLE_MS = 500,

    /** Fallback brightness if the panel never reported one. */
    DISPLAY_BRIGHTNESS_DEFAULT = 100,
};

static SemaphoreHandle_t s_state_mutex;
static TaskHandle_t s_button_task;
static bool s_asleep;
static int s_awake_brightness = DISPLAY_BRIGHTNESS_DEFAULT;

bool watch_sleep_is_asleep(void)
{
    return s_asleep;
}

static void state_lock(void)
{
    if (s_state_mutex != NULL) {
        xSemaphoreTake(s_state_mutex, portMAX_DELAY);
    }
}

static void state_unlock(void)
{
    if (s_state_mutex != NULL) {
        xSemaphoreGive(s_state_mutex);
    }
}

esp_err_t watch_sleep_enter(void)
{
    state_lock();

    if (s_asleep) {
        state_unlock();
        return ESP_OK;
    }

    const int brightness = bsp_display_brightness_get();
    if (brightness > 0) {
        s_awake_brightness = brightness;
    }

    esp_err_t err = bsp_display_brightness_set(0);
    if (err == ESP_OK) {
        /* Stop the LVGL task last: it is what keeps the panel refreshed. */
        esp_err_t stopped = lvgl_port_stop();
        if (stopped != ESP_OK) {
            ESP_LOGW(TAG, "LVGL timer not stopped: %s", esp_err_to_name(stopped));
        }
        s_asleep = true;
        ESP_LOGI(TAG, "Asleep");
    } else {
        ESP_LOGE(TAG, "Failed to dim the display: %s", esp_err_to_name(err));
    }

    state_unlock();

    return err;
}

esp_err_t watch_sleep_exit(void)
{
    state_lock();

    if (!s_asleep) {
        state_unlock();
        return ESP_OK;
    }

    esp_err_t resumed = lvgl_port_resume();
    if (resumed != ESP_OK) {
        ESP_LOGW(TAG, "LVGL timer not resumed: %s", esp_err_to_name(resumed));
    }

    esp_err_t err = bsp_display_brightness_set(s_awake_brightness);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to restore the display: %s", esp_err_to_name(err));
    }

    s_asleep = false;
    ESP_LOGI(TAG, "Awake");

    state_unlock();

    return err;
}

esp_err_t watch_sleep_power_off(void)
{
    ESP_LOGI(TAG, "Powering off");

    /* Blank the panel first; the rails fall slowly enough to show a torn frame. */
    const bool was_asleep = s_asleep;
    watch_sleep_enter();

    uint8_t config = 0;
    ESP_RETURN_ON_ERROR(
        watch_power_read_register(AXP2101_REG_COMMON_CONFIG, &config),
        TAG, "failed to read the PMU configuration"
    );
    ESP_RETURN_ON_ERROR(
        watch_power_write_register(
            AXP2101_REG_COMMON_CONFIG, (uint8_t)(config | AXP2101_SOFT_POWEROFF)
        ),
        TAG, "failed to request a power off"
    );

    vTaskDelay(pdMS_TO_TICKS(POWER_OFF_SETTLE_MS));

    /* Only reached while VBUS keeps the PMU alive, i.e. plugged into USB. */
    ESP_LOGW(TAG, "Still powered after a power-off request; USB attached?");

    if (!was_asleep) {
        watch_sleep_exit();
    }

    return ESP_OK;
}

static void handle_power_button(void)
{
    if (s_asleep) {
        watch_sleep_exit();
    } else {
        watch_sleep_enter();
    }
}

/**
 * Drain the PMU's PWRON interrupt flags and act on the button.
 *
 * A short press toggles sleep, a long press powers the watch off. Only the
 * PWRON bits are acknowledged; the VBUS and battery flags in the same register
 * belong to whoever adds charger handling later.
 */
static void button_task(void *arg)
{
    (void)arg;

    while (true) {
        uint8_t status = 0;

        if (watch_power_read_register(AXP2101_REG_IRQ_STATUS_1, &status) == ESP_OK) {
            const uint8_t pending = status & AXP2101_IRQ_PWRON_MASK;

            if (pending != 0) {
                watch_power_write_register(AXP2101_REG_IRQ_STATUS_1, pending);

                if (pending & AXP2101_IRQ_PWRON_LONG) {
                    watch_sleep_power_off();
                } else if (pending & AXP2101_IRQ_PWRON_SHORT) {
                    handle_power_button();
                }
            }
        }

        vTaskDelay(pdMS_TO_TICKS(BUTTON_POLL_PERIOD_MS));
    }
}

esp_err_t watch_sleep_init(void)
{
    if (s_button_task != NULL) {
        return ESP_OK;
    }

    uint8_t enabled = 0;
    ESP_RETURN_ON_ERROR(
        watch_power_read_register(AXP2101_REG_IRQ_ENABLE_1, &enabled),
        TAG, "failed to read the PMU IRQ enables"
    );
    ESP_RETURN_ON_ERROR(
        watch_power_write_register(
            AXP2101_REG_IRQ_ENABLE_1,
            (uint8_t)(enabled | AXP2101_IRQ_PWRON_SHORT | AXP2101_IRQ_PWRON_LONG)
        ),
        TAG, "failed to enable the PWR button IRQs"
    );

    /* Presses that happened during boot would otherwise sleep us immediately. */
    ESP_RETURN_ON_ERROR(
        watch_power_write_register(AXP2101_REG_IRQ_STATUS_1, AXP2101_IRQ_PWRON_MASK),
        TAG, "failed to clear stale PWR button events"
    );

    const int brightness = bsp_display_brightness_get();
    if (brightness > 0) {
        s_awake_brightness = brightness;
    }

    if (s_state_mutex == NULL) {
        s_state_mutex = xSemaphoreCreateMutex();
        ESP_RETURN_ON_FALSE(s_state_mutex != NULL, ESP_ERR_NO_MEM, TAG, "no memory for the mutex");
    }

    const BaseType_t created = xTaskCreate(
        button_task, "watch_sleep", BUTTON_TASK_STACK_SIZE, NULL, BUTTON_TASK_PRIORITY,
        &s_button_task
    );
    ESP_RETURN_ON_FALSE(created == pdPASS, ESP_ERR_NO_MEM, TAG, "failed to start the button task");

    ESP_LOGI(TAG, "PWR button toggles sleep, long press powers off");

    return ESP_OK;
}
