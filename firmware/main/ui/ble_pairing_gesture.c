#include "ble_pairing_gesture.h"

#include <string.h>

void ble_pairing_gesture_init(ble_pairing_gesture_t *gesture)
{
    if (gesture != NULL) {
        memset(gesture, 0, sizeof(*gesture));
    }
}

ble_pairing_gesture_event_t ble_pairing_gesture_update(ble_pairing_gesture_t *gesture,
                                                       bool s7_down, int64_t now_us)
{
    if (gesture == NULL) {
        return BLE_PAIRING_GESTURE_NONE;
    }

    if (s7_down && !gesture->down) {
        gesture->down = true;
        gesture->consumed = false;
        gesture->down_since_us = now_us;
        return BLE_PAIRING_GESTURE_NONE;
    }

    if (s7_down && gesture->down && !gesture->consumed &&
        now_us - gesture->down_since_us >= BLE_PAIRING_HOLD_US) {
        gesture->consumed = true;
        return BLE_PAIRING_GESTURE_OPEN_WINDOW;
    }

    if (!s7_down && gesture->down) {
        const int64_t held_us = now_us - gesture->down_since_us;
        const bool consumed = gesture->consumed;
        gesture->down = false;
        gesture->consumed = false;
        gesture->down_since_us = 0;
        if (consumed) {
            return BLE_PAIRING_GESTURE_NONE;
        }
        return held_us < BLE_PAIRING_SHORT_TAP_US ? BLE_PAIRING_GESTURE_SHORT_TAP
                                                  : BLE_PAIRING_GESTURE_CANCELLED;
    }

    return BLE_PAIRING_GESTURE_NONE;
}

void ble_pairing_gesture_cancel(ble_pairing_gesture_t *gesture)
{
    ble_pairing_gesture_init(gesture);
}
