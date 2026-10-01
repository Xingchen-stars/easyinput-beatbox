#include "device_mode_selector.h"

#include <assert.h>
#include <stdio.h>

static void test_short_press_is_forwarded_after_release(void)
{
    device_mode_selector_t selector;
    device_mode_selector_init(&selector, 2);

    assert(device_mode_selector_on_button(&selector, true, 0, 1000000) ==
           DEVICE_MODE_SELECTOR_EVENT_NONE);
    assert(device_mode_selector_poll(&selector, 5999999) ==
           DEVICE_MODE_SELECTOR_EVENT_NONE);
    assert(device_mode_selector_on_button(&selector, false, 0, 5999999) ==
           DEVICE_MODE_SELECTOR_EVENT_RELEASED_BEFORE_HOLD);
    assert(device_mode_selector_last_press_duration_us(&selector) == 4999999);
}

static void test_three_to_five_second_release_is_available_to_easyinput(void)
{
    device_mode_selector_t selector;
    device_mode_selector_init(&selector, 2);

    (void)device_mode_selector_on_button(&selector, true, 1, 10000000);
    assert(device_mode_selector_on_button(&selector, false, 1, 14000000) ==
           DEVICE_MODE_SELECTOR_EVENT_RELEASED_BEFORE_HOLD);
    assert(device_mode_selector_last_press_duration_us(&selector) == 4000000);
}

static void test_five_second_hold_enters_and_consumes_release(void)
{
    device_mode_selector_t selector;
    device_mode_selector_init(&selector, 2);

    (void)device_mode_selector_on_button(&selector, true, 1, 2000000);
    assert(device_mode_selector_poll(&selector, 6999999) ==
           DEVICE_MODE_SELECTOR_EVENT_NONE);
    assert(device_mode_selector_poll(&selector, 7000000) ==
           DEVICE_MODE_SELECTOR_EVENT_ENTERED);
    assert(device_mode_selector_is_active(&selector));
    assert(device_mode_selector_selected_index(&selector) == 1);
    assert(device_mode_selector_on_button(&selector, false, 1, 7100000) ==
           DEVICE_MODE_SELECTOR_EVENT_NONE);
    assert(device_mode_selector_is_active(&selector));
}

static void test_rotate_wraps_and_short_press_confirms(void)
{
    device_mode_selector_t selector;
    device_mode_selector_init(&selector, 3);

    (void)device_mode_selector_on_button(&selector, true, 1, 3000000);
    assert(device_mode_selector_poll(&selector, 8000000) ==
           DEVICE_MODE_SELECTOR_EVENT_ENTERED);
    (void)device_mode_selector_on_button(&selector, false, 1, 8100000);

    assert(device_mode_selector_on_rotate(&selector, 1, 8200000) ==
           DEVICE_MODE_SELECTOR_EVENT_PREVIEW_CHANGED);
    assert(device_mode_selector_selected_index(&selector) == 2);
    assert(device_mode_selector_on_rotate(&selector, 1, 8300000) ==
           DEVICE_MODE_SELECTOR_EVENT_PREVIEW_CHANGED);
    assert(device_mode_selector_selected_index(&selector) == 0);
    assert(device_mode_selector_on_rotate(&selector, -1, 8400000) ==
           DEVICE_MODE_SELECTOR_EVENT_PREVIEW_CHANGED);
    assert(device_mode_selector_selected_index(&selector) == 2);

    assert(device_mode_selector_on_button(&selector, true, 1, 8500000) ==
           DEVICE_MODE_SELECTOR_EVENT_NONE);
    assert(device_mode_selector_on_button(&selector, false, 1, 8600000) ==
           DEVICE_MODE_SELECTOR_EVENT_CONFIRMED);
    assert(!device_mode_selector_is_active(&selector));
    assert(device_mode_selector_selected_index(&selector) == 2);
}

static void test_idle_picker_times_out_without_confirming(void)
{
    device_mode_selector_t selector;
    device_mode_selector_init(&selector, 2);

    (void)device_mode_selector_on_button(&selector, true, 0, 4000000);
    assert(device_mode_selector_poll(&selector, 9000000) ==
           DEVICE_MODE_SELECTOR_EVENT_ENTERED);
    (void)device_mode_selector_on_button(&selector, false, 0, 9100000);
    assert(device_mode_selector_poll(&selector, 19099999) ==
           DEVICE_MODE_SELECTOR_EVENT_NONE);
    assert(device_mode_selector_poll(&selector, 19100000) ==
           DEVICE_MODE_SELECTOR_EVENT_CANCELLED);
    assert(!device_mode_selector_is_active(&selector));
}

static void test_cancel_while_held_does_not_leak_a_short_click(void)
{
    device_mode_selector_t selector;
    device_mode_selector_init(&selector, 2);

    (void)device_mode_selector_on_button(&selector, true, 0, 5000000);
    device_mode_selector_cancel(&selector);
    assert(device_mode_selector_on_button(&selector, false, 0, 5100000) ==
           DEVICE_MODE_SELECTOR_EVENT_NONE);
}

int main(void)
{
    test_short_press_is_forwarded_after_release();
    test_three_to_five_second_release_is_available_to_easyinput();
    test_five_second_hold_enters_and_consumes_release();
    test_rotate_wraps_and_short_press_confirms();
    test_idle_picker_times_out_without_confirming();
    test_cancel_while_held_does_not_leak_a_short_click();
    puts("PASS: device mode selector hold, rotate, confirm, timeout");
    return 0;
}
