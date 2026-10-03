#include <stdbool.h>
#include <string.h>

#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "esp_err.h"
#include "esp_check.h"
#include "esp_log.h"
#include "esp_codec_dev.h"
#include "bsp/esp-bsp.h"

#include "watch_audio.h"

static const char *TAG = "watch_audio";

enum {
    /** Bytes handed to the codec per write; the I2S DMA paces the loop. */
    AUDIO_CHUNK_SIZE = 4096,
    AUDIO_TASK_STACK_SIZE = 4096,
    AUDIO_TASK_PRIORITY = 5,
    AUDIO_DEFAULT_VOLUME = 70,
};

/** The 16 kHz mono sample, linked into the app image by EMBED_FILES. */
extern const uint8_t gong_wav_start[] asm("_binary_gong_wav_start");
extern const uint8_t gong_wav_end[] asm("_binary_gong_wav_end");

/** PCM body of a parsed WAV plus the format the codec has to be opened with. */
typedef struct {
    const uint8_t *pcm;
    size_t pcm_size;
    uint32_t sample_rate;
    uint8_t channels;
    uint8_t bits_per_sample;
} wav_sample_t;

static esp_codec_dev_handle_t s_speaker;
static TaskHandle_t s_playback_task;
static wav_sample_t s_gong;
static int s_volume = AUDIO_DEFAULT_VOLUME;
static volatile bool s_playing;

static uint32_t read_u32(const uint8_t *data)
{
    return (uint32_t)data[0] | ((uint32_t)data[1] << 8) | ((uint32_t)data[2] << 16) |
           ((uint32_t)data[3] << 24);
}

static uint16_t read_u16(const uint8_t *data)
{
    return (uint16_t)((uint16_t)data[0] | ((uint16_t)data[1] << 8));
}

/**
 * Locate the format and PCM payload inside a RIFF/WAVE file.
 *
 * The chunk list is walked rather than assuming the usual 44-byte header, so
 * extra chunks (LIST, fact, ...) that encoders like to add do not shift the
 * audio data out from under us.
 */
static esp_err_t wav_parse(const uint8_t *data, size_t size, wav_sample_t *out)
{
    enum {
        RIFF_HEADER_SIZE = 12,
        CHUNK_HEADER_SIZE = 8,
        WAVE_FORMAT_PCM = 1,
    };

    if (size < RIFF_HEADER_SIZE || memcmp(data, "RIFF", 4) != 0 ||
        memcmp(data + 8, "WAVE", 4) != 0) {
        return ESP_ERR_INVALID_ARG;
    }

    bool have_format = false;
    size_t offset = RIFF_HEADER_SIZE;

    while (offset + CHUNK_HEADER_SIZE <= size) {
        const uint8_t *chunk_id = data + offset;
        const uint32_t chunk_size = read_u32(data + offset + 4);
        const size_t body = offset + CHUNK_HEADER_SIZE;

        if (chunk_size > size - body) {
            return ESP_ERR_INVALID_SIZE;
        }

        if (memcmp(chunk_id, "fmt ", 4) == 0 && chunk_size >= 16) {
            if (read_u16(data + body) != WAVE_FORMAT_PCM) {
                return ESP_ERR_NOT_SUPPORTED;
            }
            out->channels = (uint8_t)read_u16(data + body + 2);
            out->sample_rate = read_u32(data + body + 4);
            out->bits_per_sample = (uint8_t)read_u16(data + body + 14);
            have_format = true;
        } else if (memcmp(chunk_id, "data", 4) == 0) {
            if (!have_format) {
                return ESP_ERR_INVALID_ARG;
            }
            out->pcm = data + body;
            out->pcm_size = chunk_size;
            return ESP_OK;
        }

        /* RIFF chunks are word-aligned, so odd-sized bodies carry a pad byte. */
        offset = body + chunk_size + (chunk_size & 1u);
    }

    return ESP_ERR_NOT_FOUND;
}

/**
 * Render @p count strikes of @p sample back to back.
 *
 * The codec is opened once for the whole sequence: re-opening it between
 * strikes would power-cycle the amplifier and add an audible gap, and the
 * sample is short enough (1 s) that repeating it in full already sounds like
 * fast, separate hits.
 */
static void play(const wav_sample_t *sample, int count)
{
    const esp_codec_dev_sample_info_t format = {
        .bits_per_sample = sample->bits_per_sample,
        .channel = sample->channels,
        .sample_rate = sample->sample_rate,
    };

    esp_err_t err = esp_codec_dev_open(s_speaker, (esp_codec_dev_sample_info_t *)&format);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "Failed to open the speaker: %s", esp_err_to_name(err));
        return;
    }

    esp_codec_dev_set_out_vol(s_speaker, s_volume);

    for (int strike = 0; strike < count; strike++) {
        for (size_t sent = 0; sent < sample->pcm_size;) {
            size_t chunk = sample->pcm_size - sent;
            if (chunk > AUDIO_CHUNK_SIZE) {
                chunk = AUDIO_CHUNK_SIZE;
            }

            err = esp_codec_dev_write(s_speaker, (void *)(sample->pcm + sent), (int)chunk);
            if (err != ESP_OK) {
                ESP_LOGE(TAG, "Playback aborted: %s", esp_err_to_name(err));
                strike = count;
                break;
            }

            sent += chunk;
        }
    }

    esp_codec_dev_close(s_speaker);
}

/**
 * Render sounds off the caller's thread.
 *
 * Playback blocks for as long as the sample lasts, which would stall the LVGL
 * task if it ran inside a button callback, so requests are handed here through
 * a task notification instead. The notification value carries the number of
 * strikes to render.
 */
static void playback_task(void *argument)
{
    (void)argument;

    for (;;) {
        uint32_t count = 0;
        if (xTaskNotifyWait(0, UINT32_MAX, &count, portMAX_DELAY) != pdPASS) {
            continue;
        }

        play(&s_gong, (int)count);
        s_playing = false;
    }
}

esp_err_t watch_audio_set_volume(int percent)
{
    if (percent < 0 || percent > 100) {
        return ESP_ERR_INVALID_ARG;
    }

    s_volume = percent;

    return ESP_OK;
}

esp_err_t watch_audio_play_gongs(int count)
{
    if (s_playback_task == NULL) {
        return ESP_ERR_INVALID_STATE;
    }
    if (s_playing) {
        return ESP_OK;
    }

    if (count < 1) {
        count = 1;
    } else if (count > WATCH_AUDIO_MAX_GONGS) {
        count = WATCH_AUDIO_MAX_GONGS;
    }

    s_playing = true;
    xTaskNotify(s_playback_task, (uint32_t)count, eSetValueWithOverwrite);

    return ESP_OK;
}

esp_err_t watch_audio_play_gong(void)
{
    return watch_audio_play_gongs(1);
}

esp_err_t watch_audio_init(void)
{
    if (s_playback_task != NULL) {
        return ESP_OK;
    }

    ESP_RETURN_ON_ERROR(
        wav_parse(gong_wav_start, (size_t)(gong_wav_end - gong_wav_start), &s_gong),
        TAG, "the embedded gong is not a usable PCM WAV"
    );

    s_speaker = bsp_audio_codec_speaker_init();
    ESP_RETURN_ON_FALSE(
        s_speaker != NULL, ESP_FAIL, TAG, "failed to bring up the ES8311 speaker"
    );

    ESP_RETURN_ON_FALSE(
        xTaskCreate(
            playback_task, "audio", AUDIO_TASK_STACK_SIZE, NULL, AUDIO_TASK_PRIORITY,
            &s_playback_task
        ) == pdPASS,
        ESP_ERR_NO_MEM, TAG, "failed to start the playback task"
    );

    ESP_LOGI(
        TAG, "Speaker ready, gong is %u Hz %u-bit, %u bytes",
        (unsigned)s_gong.sample_rate, (unsigned)s_gong.bits_per_sample,
        (unsigned)s_gong.pcm_size
    );

    return ESP_OK;
}
