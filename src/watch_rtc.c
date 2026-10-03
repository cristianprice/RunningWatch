#include <stdlib.h>
#include <string.h>
#include <sys/time.h>

#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "driver/i2c_master.h"
#include "bsp/esp-bsp.h"

#include "watch_rtc.h"

static const char *TAG = "rtc";

enum {
    /** 7-bit I2C address of the PCF85063, fixed in silicon. */
    PCF85063_I2C_ADDRESS = 0x51,
    PCF85063_I2C_SPEED_HZ = 400000,
    PCF85063_I2C_TIMEOUT_MS = 100,

    PCF85063_REG_CONTROL_1 = 0x00,
    PCF85063_REG_SECONDS = 0x04,

    /** CONTROL_1 bit that freezes the counters while new values are written. */
    PCF85063_CONTROL_1_STOP = 0x20,
    /** Seconds-register bit raised when the oscillator lost time. */
    PCF85063_SECONDS_OS = 0x80,

    /** Number of registers holding the date/time block (0x04..0x0A). */
    PCF85063_TIME_REG_COUNT = 7,
};

static i2c_master_dev_handle_t s_rtc_device;

static uint8_t bcd_to_bin(uint8_t bcd)
{
    return (uint8_t)(((bcd >> 4) * 10) + (bcd & 0x0F));
}

static uint8_t bin_to_bcd(uint8_t bin)
{
    return (uint8_t)(((bin / 10) << 4) | (bin % 10));
}

static esp_err_t rtc_read(uint8_t reg, uint8_t *data, size_t len)
{
    return i2c_master_transmit_receive(
        s_rtc_device, &reg, 1, data, len, PCF85063_I2C_TIMEOUT_MS
    );
}

static esp_err_t rtc_write(uint8_t reg, const uint8_t *data, size_t len)
{
    uint8_t buffer[1 + PCF85063_TIME_REG_COUNT];

    if (len > sizeof(buffer) - 1) {
        return ESP_ERR_INVALID_SIZE;
    }

    buffer[0] = reg;
    memcpy(&buffer[1], data, len);

    return i2c_master_transmit(s_rtc_device, buffer, len + 1, PCF85063_I2C_TIMEOUT_MS);
}

static esp_err_t rtc_write_byte(uint8_t reg, uint8_t value)
{
    return rtc_write(reg, &value, 1);
}

/**
 * Parse the firmware build timestamp (__DATE__ / __TIME__) into a UTC epoch.
 *
 * Used only as a fallback when the RTC has never been set; the build stamp is
 * local time on the build host, which is close enough to keep the first boot
 * from showing 1970.
 */
static time_t build_timestamp(void)
{
    static const char month_names[] = "JanFebMarAprMayJunJulAugSepOctNovDec";
    const char date[] = __DATE__; /* "Mmm dd yyyy" */
    const char clock[] = __TIME__; /* "hh:mm:ss" */

    struct tm build = {0};
    const char *month = strstr(month_names, date);

    build.tm_mon = (month != NULL) ? (int)((month - month_names) / 3) : 0;
    build.tm_mday = atoi(date + 4);
    build.tm_year = atoi(date + 7) - 1900;
    build.tm_hour = atoi(clock);
    build.tm_min = atoi(clock + 3);
    build.tm_sec = atoi(clock + 6);
    build.tm_isdst = -1;

    return mktime(&build);
}

/**
 * Convert a broken-down UTC date to an epoch value.
 *
 * Hand-rolled because newlib's timegm() is not exposed here and mktime() would
 * apply the local timezone to a value that is already UTC.
 */
static time_t utc_to_epoch(const struct tm *utc_parts)
{
    int year = utc_parts->tm_year + 1900;
    int month = utc_parts->tm_mon + 1;

    /* Days-from-civil: shift the year so it starts in March, after any leap day. */
    year -= (month <= 2) ? 1 : 0;
    const int era = ((year >= 0) ? year : year - 399) / 400;
    const unsigned year_of_era = (unsigned)(year - era * 400);
    const unsigned day_of_year =
        (unsigned)((153 * (month + ((month > 2) ? -3 : 9)) + 2) / 5 + utc_parts->tm_mday - 1);
    const unsigned day_of_era =
        year_of_era * 365 + year_of_era / 4 - year_of_era / 100 + day_of_year;
    const long days = (long)era * 146097 + (long)day_of_era - 719468;

    return (time_t)(days * 86400L + utc_parts->tm_hour * 3600L + utc_parts->tm_min * 60L +
                    utc_parts->tm_sec);
}

esp_err_t watch_rtc_get_time(time_t *out_utc)
{
    if (out_utc == NULL) {
        return ESP_ERR_INVALID_ARG;
    }
    if (s_rtc_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    uint8_t regs[PCF85063_TIME_REG_COUNT];
    esp_err_t err = rtc_read(PCF85063_REG_SECONDS, regs, sizeof(regs));
    if (err != ESP_OK) {
        return err;
    }

    if (regs[0] & PCF85063_SECONDS_OS) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm stored = {0};
    stored.tm_sec = bcd_to_bin(regs[0] & 0x7F);
    stored.tm_min = bcd_to_bin(regs[1] & 0x7F);
    stored.tm_hour = bcd_to_bin(regs[2] & 0x3F);
    stored.tm_mday = bcd_to_bin(regs[3] & 0x3F);
    stored.tm_wday = regs[4] & 0x07;
    stored.tm_mon = bcd_to_bin(regs[5] & 0x1F) - 1;
    stored.tm_year = bcd_to_bin(regs[6]) + 100; /* register counts from 2000 */
    stored.tm_isdst = 0;

    /* The chip holds UTC, so convert without applying the local timezone. */
    *out_utc = utc_to_epoch(&stored);

    return ESP_OK;
}

esp_err_t watch_rtc_set_time(time_t utc)
{
    if (s_rtc_device == NULL) {
        return ESP_ERR_INVALID_STATE;
    }

    struct tm utc_parts;
    gmtime_r(&utc, &utc_parts);

    uint8_t regs[PCF85063_TIME_REG_COUNT];
    regs[0] = bin_to_bcd((uint8_t)utc_parts.tm_sec); /* clears OS as a side effect */
    regs[1] = bin_to_bcd((uint8_t)utc_parts.tm_min);
    regs[2] = bin_to_bcd((uint8_t)utc_parts.tm_hour);
    regs[3] = bin_to_bcd((uint8_t)utc_parts.tm_mday);
    regs[4] = (uint8_t)utc_parts.tm_wday;
    regs[5] = bin_to_bcd((uint8_t)(utc_parts.tm_mon + 1));
    regs[6] = bin_to_bcd((uint8_t)(utc_parts.tm_year - 100));

    /* Counters must be frozen while the seven registers are written. */
    ESP_RETURN_ON_ERROR(
        rtc_write_byte(PCF85063_REG_CONTROL_1, PCF85063_CONTROL_1_STOP),
        TAG, "failed to stop the RTC"
    );
    ESP_RETURN_ON_ERROR(
        rtc_write(PCF85063_REG_SECONDS, regs, sizeof(regs)), TAG, "failed to write the RTC"
    );
    ESP_RETURN_ON_ERROR(
        rtc_write_byte(PCF85063_REG_CONTROL_1, 0x00), TAG, "failed to restart the RTC"
    );

    struct timeval now = {.tv_sec = utc, .tv_usec = 0};
    settimeofday(&now, NULL);

    return ESP_OK;
}

esp_err_t watch_rtc_init(void)
{
    setenv("TZ", WATCH_RTC_TIMEZONE, 1);
    tzset();

    if (s_rtc_device == NULL) {
        const i2c_device_config_t device_config = {
            .dev_addr_length = I2C_ADDR_BIT_LEN_7,
            .device_address = PCF85063_I2C_ADDRESS,
            .scl_speed_hz = PCF85063_I2C_SPEED_HZ,
        };

        ESP_RETURN_ON_ERROR(
            i2c_master_bus_add_device(bsp_i2c_get_handle(), &device_config, &s_rtc_device),
            TAG, "failed to attach the PCF85063"
        );
    }

    time_t stored = 0;
    esp_err_t err = watch_rtc_get_time(&stored);
    if (err == ESP_OK) {
        struct timeval now = {.tv_sec = stored, .tv_usec = 0};
        settimeofday(&now, NULL);
        ESP_LOGI(TAG, "System clock restored from the RTC");
        return ESP_OK;
    }

    if (err != ESP_ERR_INVALID_STATE) {
        ESP_LOGE(TAG, "Failed to read the RTC: %s", esp_err_to_name(err));
        return err;
    }

    ESP_LOGW(TAG, "RTC was never set, seeding it with the build timestamp");

    return watch_rtc_set_time(build_timestamp());
}
