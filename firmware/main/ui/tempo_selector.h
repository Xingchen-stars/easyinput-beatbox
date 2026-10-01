#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define TEMPO_SELECTOR_PRESET_COUNT     3
#define TEMPO_SELECTOR_DOUBLE_CLICK_US 350000LL
#define TEMPO_SELECTOR_TIMEOUT_US      5000000LL

typedef enum {
    TEMPO_SELECTOR_EVENT_NONE = 0,
    TEMPO_SELECTOR_EVENT_TRANSPORT,
    TEMPO_SELECTOR_EVENT_ENTERED,
    TEMPO_SELECTOR_EVENT_PREVIEW_CHANGED,
    TEMPO_SELECTOR_EVENT_CONFIRMED,
    TEMPO_SELECTOR_EVENT_CANCELLED,
} tempo_selector_event_t;

typedef struct {
    bool active;
    bool single_click_pending;
    uint8_t selected_index;
    int64_t first_click_us;
    int64_t last_action_us;
} tempo_selector_t;

void tempo_selector_init(tempo_selector_t *selector);

/**
 * Feed one debounced encoder-button press.
 *
 * Outside selection mode the first press waits for the double-click window.
 * A second press inside that window enters selection mode. Inside selection
 * mode one press confirms immediately.
 */
tempo_selector_event_t tempo_selector_on_encoder_click(tempo_selector_t *selector,
                                                        uint16_t current_bpm,
                                                        int64_t now_us);

/** Rotate through the three presets while selection mode is active. */
tempo_selector_event_t tempo_selector_on_rotate(tempo_selector_t *selector, int8_t detents,
                                                 int64_t now_us);

/** Resolve a pending single click or cancel an inactive selection after timeout. */
tempo_selector_event_t tempo_selector_poll(tempo_selector_t *selector, int64_t now_us);

/** Cancel selection and any delayed click without changing BPM. */
void tempo_selector_cancel(tempo_selector_t *selector);

bool tempo_selector_is_active(const tempo_selector_t *selector);
uint8_t tempo_selector_selected_index(const tempo_selector_t *selector);
uint16_t tempo_selector_selected_bpm(const tempo_selector_t *selector);

#ifdef __cplusplus
}
#endif
