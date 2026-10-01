#include "tempo_selector.h"

#include <limits.h>
#include <stddef.h>

static const uint16_t s_preset_bpms[TEMPO_SELECTOR_PRESET_COUNT] = {90, 120, 140};

static uint8_t nearest_preset_index(uint16_t bpm)
{
    uint8_t nearest = 0;
    uint16_t nearest_distance = UINT16_MAX;
    for (uint8_t i = 0; i < TEMPO_SELECTOR_PRESET_COUNT; ++i) {
        const uint16_t preset = s_preset_bpms[i];
        const uint16_t distance = bpm > preset ? (uint16_t)(bpm - preset)
                                               : (uint16_t)(preset - bpm);
        if (distance < nearest_distance) {
            nearest = i;
            nearest_distance = distance;
        }
    }
    return nearest;
}

void tempo_selector_init(tempo_selector_t *selector)
{
    if (selector == NULL) {
        return;
    }
    *selector = (tempo_selector_t){0};
}

tempo_selector_event_t tempo_selector_on_encoder_click(tempo_selector_t *selector,
                                                        uint16_t current_bpm,
                                                        int64_t now_us)
{
    if (selector == NULL) {
        return TEMPO_SELECTOR_EVENT_NONE;
    }

    if (selector->active) {
        selector->active = false;
        selector->single_click_pending = false;
        selector->last_action_us = now_us;
        return TEMPO_SELECTOR_EVENT_CONFIRMED;
    }

    if (selector->single_click_pending &&
        now_us - selector->first_click_us <= TEMPO_SELECTOR_DOUBLE_CLICK_US) {
        selector->single_click_pending = false;
        selector->active = true;
        selector->selected_index = nearest_preset_index(current_bpm);
        selector->last_action_us = now_us;
        return TEMPO_SELECTOR_EVENT_ENTERED;
    }

    selector->single_click_pending = true;
    selector->first_click_us = now_us;
    return TEMPO_SELECTOR_EVENT_NONE;
}

tempo_selector_event_t tempo_selector_on_rotate(tempo_selector_t *selector, int8_t detents,
                                                 int64_t now_us)
{
    if (selector == NULL || !selector->active || detents == 0) {
        return TEMPO_SELECTOR_EVENT_NONE;
    }

    int next = (int)selector->selected_index + (int)detents;
    next %= TEMPO_SELECTOR_PRESET_COUNT;
    if (next < 0) {
        next += TEMPO_SELECTOR_PRESET_COUNT;
    }
    selector->selected_index = (uint8_t)next;
    selector->last_action_us = now_us;
    return TEMPO_SELECTOR_EVENT_PREVIEW_CHANGED;
}

tempo_selector_event_t tempo_selector_poll(tempo_selector_t *selector, int64_t now_us)
{
    if (selector == NULL) {
        return TEMPO_SELECTOR_EVENT_NONE;
    }

    if (selector->active) {
        if (now_us - selector->last_action_us >= TEMPO_SELECTOR_TIMEOUT_US) {
            selector->active = false;
            return TEMPO_SELECTOR_EVENT_CANCELLED;
        }
        return TEMPO_SELECTOR_EVENT_NONE;
    }

    if (selector->single_click_pending &&
        now_us - selector->first_click_us > TEMPO_SELECTOR_DOUBLE_CLICK_US) {
        selector->single_click_pending = false;
        return TEMPO_SELECTOR_EVENT_TRANSPORT;
    }
    return TEMPO_SELECTOR_EVENT_NONE;
}

void tempo_selector_cancel(tempo_selector_t *selector)
{
    if (selector == NULL) {
        return;
    }
    selector->active = false;
    selector->single_click_pending = false;
}

bool tempo_selector_is_active(const tempo_selector_t *selector)
{
    return selector != NULL && selector->active;
}

uint8_t tempo_selector_selected_index(const tempo_selector_t *selector)
{
    if (selector == NULL || selector->selected_index >= TEMPO_SELECTOR_PRESET_COUNT) {
        return 0;
    }
    return selector->selected_index;
}

uint16_t tempo_selector_selected_bpm(const tempo_selector_t *selector)
{
    return s_preset_bpms[tempo_selector_selected_index(selector)];
}
