#include "device_mode_selector.h"

#include <stddef.h>

static uint8_t normalize_index(const device_mode_selector_t *selector, uint8_t index)
{
    if (selector == NULL || selector->mode_count == 0) {
        return 0;
    }
    return (uint8_t)(index % selector->mode_count);
}

void device_mode_selector_init(device_mode_selector_t *selector, uint8_t mode_count)
{
    if (selector == NULL) {
        return;
    }
    *selector = (device_mode_selector_t){0};
    selector->mode_count = mode_count == 0 ? 1 : mode_count;
}

device_mode_selector_event_t device_mode_selector_on_button(
    device_mode_selector_t *selector, bool down, uint8_t current_mode_index,
    int64_t now_us)
{
    if (selector == NULL || down == selector->button_down) {
        return DEVICE_MODE_SELECTOR_EVENT_NONE;
    }

    selector->button_down = down;
    if (down) {
        selector->button_down_us = now_us;
        selector->hold_origin_index = normalize_index(selector, current_mode_index);
        return DEVICE_MODE_SELECTOR_EVENT_NONE;
    }

    selector->last_press_duration_us = now_us - selector->button_down_us;

    if (selector->consume_release) {
        selector->consume_release = false;
        selector->last_action_us = now_us;
        return DEVICE_MODE_SELECTOR_EVENT_NONE;
    }

    if (selector->active) {
        selector->active = false;
        selector->last_action_us = now_us;
        return DEVICE_MODE_SELECTOR_EVENT_CONFIRMED;
    }

    return DEVICE_MODE_SELECTOR_EVENT_RELEASED_BEFORE_HOLD;
}

device_mode_selector_event_t device_mode_selector_on_rotate(
    device_mode_selector_t *selector, int8_t detents, int64_t now_us)
{
    if (selector == NULL || !selector->active || detents == 0 ||
        selector->mode_count == 0) {
        return DEVICE_MODE_SELECTOR_EVENT_NONE;
    }

    int next = (int)selector->selected_index + (int)detents;
    next %= (int)selector->mode_count;
    if (next < 0) {
        next += selector->mode_count;
    }
    selector->selected_index = (uint8_t)next;
    selector->last_action_us = now_us;
    return DEVICE_MODE_SELECTOR_EVENT_PREVIEW_CHANGED;
}

device_mode_selector_event_t device_mode_selector_poll(
    device_mode_selector_t *selector, int64_t now_us)
{
    if (selector == NULL) {
        return DEVICE_MODE_SELECTOR_EVENT_NONE;
    }

    if (selector->button_down && !selector->active && !selector->consume_release &&
        now_us - selector->button_down_us >= DEVICE_MODE_SELECTOR_HOLD_US) {
        selector->active = true;
        selector->consume_release = true;
        selector->selected_index = selector->hold_origin_index;
        selector->last_action_us = now_us;
        return DEVICE_MODE_SELECTOR_EVENT_ENTERED;
    }

    if (selector->active && !selector->button_down &&
        now_us - selector->last_action_us >= DEVICE_MODE_SELECTOR_TIMEOUT_US) {
        selector->active = false;
        return DEVICE_MODE_SELECTOR_EVENT_CANCELLED;
    }

    return DEVICE_MODE_SELECTOR_EVENT_NONE;
}

void device_mode_selector_cancel(device_mode_selector_t *selector)
{
    if (selector == NULL) {
        return;
    }
    selector->active = false;
    selector->consume_release = selector->button_down;
}

bool device_mode_selector_is_active(const device_mode_selector_t *selector)
{
    return selector != NULL && selector->active;
}

uint8_t device_mode_selector_selected_index(const device_mode_selector_t *selector)
{
    if (selector == NULL) {
        return 0;
    }
    return normalize_index(selector, selector->selected_index);
}

int64_t device_mode_selector_last_press_duration_us(
    const device_mode_selector_t *selector)
{
    return selector == NULL ? 0 : selector->last_press_duration_us;
}
