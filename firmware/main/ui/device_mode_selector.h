#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define DEVICE_MODE_SELECTOR_HOLD_US    5000000LL
#define DEVICE_MODE_SELECTOR_TIMEOUT_US 10000000LL

typedef enum {
    DEVICE_MODE_SELECTOR_EVENT_NONE = 0,
    DEVICE_MODE_SELECTOR_EVENT_RELEASED_BEFORE_HOLD,
    DEVICE_MODE_SELECTOR_EVENT_ENTERED,
    DEVICE_MODE_SELECTOR_EVENT_PREVIEW_CHANGED,
    DEVICE_MODE_SELECTOR_EVENT_CONFIRMED,
    DEVICE_MODE_SELECTOR_EVENT_CANCELLED,
} device_mode_selector_event_t;

/**
 * Pure state for the top-level device-mode picker.
 *
 * The mode count is supplied at initialization so future modes can be added
 * without changing the gesture state machine.
 */
typedef struct {
    bool active;
    bool button_down;
    bool consume_release;
    uint8_t mode_count;
    uint8_t selected_index;
    uint8_t hold_origin_index;
    int64_t button_down_us;
    int64_t last_press_duration_us;
    int64_t last_action_us;
} device_mode_selector_t;

void device_mode_selector_init(device_mode_selector_t *selector, uint8_t mode_count);

/**
 * Feed the debounced physical level whenever it changes.
 *
 * A release before five seconds returns RELEASED_BEFORE_HOLD. Beatbox treats
 * it as a normal click; EasyInput can use last_press_duration_us to preserve
 * its agreed three-to-five-second platform-selection gesture. Holding for five
 * seconds is detected by poll(); the release that follows that hold is consumed.
 */
device_mode_selector_event_t device_mode_selector_on_button(
    device_mode_selector_t *selector, bool down, uint8_t current_mode_index,
    int64_t now_us);

/** Rotate through available modes while the picker is active. */
device_mode_selector_event_t device_mode_selector_on_rotate(
    device_mode_selector_t *selector, int8_t detents, int64_t now_us);

/** Detect the five-second hold and cancel an idle picker after ten seconds. */
device_mode_selector_event_t device_mode_selector_poll(
    device_mode_selector_t *selector, int64_t now_us);

/** Cancel without allowing the current physical press to leak into a click. */
void device_mode_selector_cancel(device_mode_selector_t *selector);

bool device_mode_selector_is_active(const device_mode_selector_t *selector);
uint8_t device_mode_selector_selected_index(const device_mode_selector_t *selector);
int64_t device_mode_selector_last_press_duration_us(
    const device_mode_selector_t *selector);

#ifdef __cplusplus
}
#endif
