#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef struct {
    uint8_t step;         /* 0..15 grid step */
    uint8_t beat_in_bar;  /* 0..3 */
    uint16_t tick;        /* 0..383 within bar */
    uint32_t bar;
    bool accent;
    bool is_quarter;      /* quarter-note boundary */
    bool is_step;         /* 16th grid boundary (playhead) */
    uint64_t sample_index;
} audio_beat_event_t;

typedef enum {
    AUDIO_MODE_METRONOME = 0,
    AUDIO_MODE_DRUM = 1,
} audio_mode_t;

typedef enum {
    AUDIO_TEMPO_PROMPT_SLOW = 0,
    AUDIO_TEMPO_PROMPT_ORIGINAL,
    AUDIO_TEMPO_PROMPT_FAST,
} audio_tempo_prompt_t;

typedef enum {
    AUDIO_DEVICE_MODE_PROMPT_BEATBOX = 0,
    AUDIO_DEVICE_MODE_PROMPT_EASYINPUT,
} audio_device_mode_prompt_t;

/**
 * Continuous, sample-clocked I2S renderer.
 *
 * Internal transport runs at 96 PPQN derived from the I2S sample cursor.
 * Metronome edges, sequencer ticks and external pad notes share the same
 * voice mixer so sounds never hard-cut each other.
 */
esp_err_t audio_click_init(void);
esp_err_t audio_click_set_bpm(uint16_t bpm);
esp_err_t audio_click_set_metronome(bool enabled);
esp_err_t audio_click_set_mode(audio_mode_t mode);
esp_err_t audio_click_set_volume(uint8_t volume);
uint8_t audio_click_get_volume(void);

/**
 * @param running transport state
 * @param restart true = Start (reset position); false = Continue
 */
esp_err_t audio_click_set_running(bool running, bool restart);

/** Non-blocking position / beat event for LED/host visualization. */
bool audio_click_poll_beat(audio_beat_event_t *event);

esp_err_t audio_click_play_normal(void);
esp_err_t audio_click_play_accent(void);
esp_err_t audio_click_play_note(uint8_t note, uint8_t velocity);
/** Speak one non-blocking tempo preset name through the existing mixer. */
esp_err_t audio_click_play_tempo_prompt(audio_tempo_prompt_t prompt);
/** Speak one non-blocking top-level device-mode name. */
esp_err_t audio_click_play_device_mode_prompt(audio_device_mode_prompt_t prompt);

/** Current transport position (safe to read from main task). */
void audio_click_get_position(uint32_t *bar, uint8_t *step, uint8_t *beat, uint16_t *tick);

/** Stop transport, flush queued events, and silence all active voices. */
esp_err_t audio_click_stop(void);

#ifdef __cplusplus
}
#endif
