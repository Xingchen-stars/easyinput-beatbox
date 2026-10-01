#pragma once

#include <stdbool.h>
#include <stdint.h>

#ifdef __cplusplus
extern "C" {
#endif

#define BLE_PAIRING_HOLD_US 3000000LL
#define BLE_PAIRING_SHORT_TAP_US 280000LL

typedef enum {
    BLE_PAIRING_GESTURE_NONE = 0,
    BLE_PAIRING_GESTURE_SHORT_TAP,
    BLE_PAIRING_GESTURE_OPEN_WINDOW,
    BLE_PAIRING_GESTURE_CANCELLED,
} ble_pairing_gesture_event_t;

typedef struct {
    bool down;
    bool consumed;
    int64_t down_since_us;
} ble_pairing_gesture_t;

void ble_pairing_gesture_init(ble_pairing_gesture_t *gesture);

/**
 * Handle S7 while transport is stopped. A short tap remains A/B; holding for
 * three seconds opens the physical pairing window. Medium holds are consumed
 * so they cannot accidentally toggle A/B on release.
 */
ble_pairing_gesture_event_t ble_pairing_gesture_update(ble_pairing_gesture_t *gesture,
                                                       bool s7_down, int64_t now_us);

void ble_pairing_gesture_cancel(ble_pairing_gesture_t *gesture);

#ifdef __cplusplus
}
#endif
