#pragma once

#include <stdbool.h>
#include <stdint.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

esp_err_t led_status_init(void);
esp_err_t led_status_set_solid_rgb(uint8_t r, uint8_t g, uint8_t b);

/**
 * Trigger the next beat position on the five-pixel ping-pong path.
 */
void led_status_on_beat(uint8_t step, bool is_accent, int64_t now_us);

/** Render a smooth light-transfer animation; call roughly every 10–20 ms. */
esp_err_t led_status_update(int64_t now_us, uint16_t bpm, bool running);

/** Briefly visualize the BPM range after an encoder turn. */
void led_status_show_tempo(uint16_t bpm, int64_t now_us);

esp_err_t led_status_clear(void);

#ifdef __cplusplus
}
#endif
