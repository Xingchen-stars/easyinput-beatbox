#include "ble_pairing_gesture.h"

#include <assert.h>
#include <stdio.h>

static void test_short_tap_remains_variation(void)
{
    ble_pairing_gesture_t gesture;
    ble_pairing_gesture_init(&gesture);
    assert(ble_pairing_gesture_update(&gesture, true, 1000000) == BLE_PAIRING_GESTURE_NONE);
    assert(ble_pairing_gesture_update(&gesture, false, 1200000) ==
           BLE_PAIRING_GESTURE_SHORT_TAP);
}

static void test_medium_hold_is_consumed(void)
{
    ble_pairing_gesture_t gesture;
    ble_pairing_gesture_init(&gesture);
    (void)ble_pairing_gesture_update(&gesture, true, 2000000);
    assert(ble_pairing_gesture_update(&gesture, true, 3500000) == BLE_PAIRING_GESTURE_NONE);
    assert(ble_pairing_gesture_update(&gesture, false, 3500000) ==
           BLE_PAIRING_GESTURE_CANCELLED);
}

static void test_three_second_hold_opens_once_and_consumes_release(void)
{
    ble_pairing_gesture_t gesture;
    ble_pairing_gesture_init(&gesture);
    (void)ble_pairing_gesture_update(&gesture, true, 4000000);
    assert(ble_pairing_gesture_update(&gesture, true, 6999999) == BLE_PAIRING_GESTURE_NONE);
    assert(ble_pairing_gesture_update(&gesture, true, 7000000) ==
           BLE_PAIRING_GESTURE_OPEN_WINDOW);
    assert(ble_pairing_gesture_update(&gesture, true, 8000000) == BLE_PAIRING_GESTURE_NONE);
    assert(ble_pairing_gesture_update(&gesture, false, 8100000) == BLE_PAIRING_GESTURE_NONE);
}

int main(void)
{
    test_short_tap_remains_variation();
    test_medium_hold_is_consumed();
    test_three_second_hold_opens_once_and_consumes_release();
    puts("PASS: stopped S7 short tap and physical BLE pairing hold");
    return 0;
}
