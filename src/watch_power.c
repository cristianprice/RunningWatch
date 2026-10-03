#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"

#include "watch_power.h"

static const char *TAG = "watch_power";

enum {
    /** 7-bit I2C address of the AXP2101, fixed in silicon. */
    AXP2101_I2C_ADDRESS = 0x34,
    AXP2101_I2C_SPEED_HZ = 400000,
    AXP2101_I2C_TIMEOUT_MS = 100,

    AXP2101_REG_STATUS_1 = 0x00,
    AXP2101_REG_STATUS_2 = 0x01,
    AXP2101_REG_ADC_CHANNEL_CTRL = 0x30,
    AXP2101_REG_BAT_DETECT_CTRL = 0x68,
    AXP2101_REG_BAT_PERCENT = 0xA4,

    /** STATUS_1 bit raised while a cell is attached. */
    AXP2101_STATUS_1_BAT_PRESENT = 0x08,
    /** STATUS_2 bits 6:5 — 0 standby, 1 charging, 2 discharging. */
    AXP2101_STATUS_2_CHARGE_SHIFT = 5,
    AXP2101_STATUS_2_CHARGE_MASK = 0x03,
    AXP2101_CHARGE_STATE_CHARGING = 0x01,

    /** ADC_CHANNEL_CTRL bit enabling the battery voltage channel. */
    AXP2101_ADC_BATTERY_VOLTAGE = 0x01,
    /** BAT_DETECT_CTRL bit enabling cell detection. */
    AXP2101_BAT_DETECT_ENABLE = 0x01,
};

static i2c_master_dev_handle_t s_pmu_device;

esp_err_t watch_power_read_register(uint8_t reg, uint8_t *value)
{
    if (value == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_pmu_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    return i2c_master_transmit_receive(
        s_pmu_device, &reg, 1, value, 1, AXP2101_I2C_TIMEOUT_MS
    );
}

esp_err_t watch_power_write_register(uint8_t reg, uint8_t value)
{
    if (s_pmu_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    const uint8_t frame[] = {reg, value};

    return i2c_master_transmit(s_pmu_device, frame, sizeof(frame), AXP2101_I2C_TIMEOUT_MS);
}

static esp_err_t pmu_read(uint8_t reg, uint8_t *value)
{
    return watch_power_read_register(reg, value);
}

static esp_err_t pmu_set_bits(uint8_t reg, uint8_t bits)
{
    uint8_t value = 0;
    ESP_RETURN_ON_ERROR(pmu_read(reg, &value), TAG, "failed to read register 0x%02x", reg);

    return watch_power_write_register(reg, (uint8_t)(value | bits));
}

esp_err_t watch_power_get_battery(watch_power_battery_t *out)
{
    if (out == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_pmu_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t status_1 = 0;
    uint8_t status_2 = 0;
    ESP_RETURN_ON_ERROR(pmu_read(AXP2101_REG_STATUS_1, &status_1), TAG, "status 1");
    ESP_RETURN_ON_ERROR(pmu_read(AXP2101_REG_STATUS_2, &status_2), TAG, "status 2");

    out->present = (status_1 & AXP2101_STATUS_1_BAT_PRESENT) != 0;
    out->charging =
        ((status_2 >> AXP2101_STATUS_2_CHARGE_SHIFT) & AXP2101_STATUS_2_CHARGE_MASK) ==
        AXP2101_CHARGE_STATE_CHARGING;
    out->percent = -1;

    if (!out->present) {
        return ESP_OK;
    }

    uint8_t percent = 0;
    ESP_RETURN_ON_ERROR(pmu_read(AXP2101_REG_BAT_PERCENT, &percent), TAG, "gauge");

    /* The gauge reports 0xFF until it has settled on a first estimate. */
    if (percent <= 100) {
        out->percent = percent;
    }

    return ESP_OK;
}

esp_err_t watch_power_init(void)
{
    if (s_pmu_device == NULL) {
        const i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = AXP2101_I2C_ADDRESS,
            .scl_speed_hz = AXP2101_I2C_SPEED_HZ,
        };

        ESP_RETURN_ON_ERROR(
            i2c_master_bus_add_device(bsp_i2c_get_handle(), &device_config, &s_pmu_device),
            TAG, "failed to attach the AXP2101"
        );
    }

    ESP_RETURN_ON_ERROR(
        pmu_set_bits(AXP2101_REG_BAT_DETECT_CTRL, AXP2101_BAT_DETECT_ENABLE),
        TAG, "failed to enable battery detection"
    );
    ESP_RETURN_ON_ERROR(
        pmu_set_bits(AXP2101_REG_ADC_CHANNEL_CTRL, AXP2101_ADC_BATTERY_VOLTAGE),
        TAG, "failed to enable the battery ADC"
    );

    ESP_LOGI(TAG, "AXP2101 battery metering enabled");

    return ESP_OK;
}
