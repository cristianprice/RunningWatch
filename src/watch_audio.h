#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

/** Longest gong sequence #watch_audio_play_gongs will render. */
#define WATCH_AUDIO_MAX_GONGS 8

/**
 * Bring up the ES8311 speaker path and the playback task.
 *
 * Must be called once at start-up, before the first #watch_audio_play_gong.
 * The codec itself is opened and closed around each sound, so the amplifier is
 * only powered while something is actually playing.
 *
 * Safe to call before the display is started; it does not touch LVGL.
 *
 * @return ESP_OK on success, an esp_err_t from the codec or FreeRTOS otherwise.
 */
esp_err_t watch_audio_init(void);

/**
 * Play the gong sample.
 *
 * Returns immediately: the sample is rendered by a background task, so this is
 * safe to call from an LVGL event callback without stalling the UI. A request
 * that arrives while the gong is already playing is ignored rather than
 * queued.
 *
 * @return ESP_OK when the request was accepted, ESP_ERR_INVALID_STATE when
 *         watch_audio_init() has not run.
 */
esp_err_t watch_audio_play_gong(void);

/**
 * Play the gong several times in quick succession.
 *
 * The strikes are rendered back to back through a single codec open, so a
 * sequence sounds like repeated hits rather than separate sounds. Returns
 * immediately, like #watch_audio_play_gong.
 *
 * @param count number of strikes, clamped to 1 … #WATCH_AUDIO_MAX_GONGS.
 * @return ESP_OK when the request was accepted, ESP_ERR_INVALID_STATE when
 *         watch_audio_init() has not run.
 */
esp_err_t watch_audio_play_gongs(int count);

/**
 * Set the playback volume.
 *
 * @param percent 0 (mute) to 100 (full scale).
 * @return ESP_OK on success, an esp_err_t from the codec otherwise.
 */
esp_err_t watch_audio_set_volume(int percent);

#ifdef __cplusplus
}
#endif
