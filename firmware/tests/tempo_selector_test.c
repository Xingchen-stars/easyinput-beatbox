#include "tempo_selector.h"

#include <assert.h>
#include <stdio.h>

static void test_single_click_becomes_transport(void)
{
    tempo_selector_t selector;
    tempo_selector_init(&selector);

    assert(tempo_selector_on_encoder_click(&selector, 120, 1000000) ==
           TEMPO_SELECTOR_EVENT_NONE);
    assert(tempo_selector_poll(&selector, 1340000) == TEMPO_SELECTOR_EVENT_NONE);
    assert(tempo_selector_poll(&selector, 1350001) == TEMPO_SELECTOR_EVENT_TRANSPORT);
}

static void test_double_click_select_rotate_confirm(void)
{
    tempo_selector_t selector;
    tempo_selector_init(&selector);

    assert(tempo_selector_on_encoder_click(&selector, 118, 2000000) ==
           TEMPO_SELECTOR_EVENT_NONE);
    assert(tempo_selector_on_encoder_click(&selector, 118, 2200000) ==
           TEMPO_SELECTOR_EVENT_ENTERED);
    assert(tempo_selector_is_active(&selector));
    assert(tempo_selector_selected_bpm(&selector) == 120);

    assert(tempo_selector_on_rotate(&selector, 1, 2300000) ==
           TEMPO_SELECTOR_EVENT_PREVIEW_CHANGED);
    assert(tempo_selector_selected_bpm(&selector) == 140);
    assert(tempo_selector_on_rotate(&selector, 1, 2400000) ==
           TEMPO_SELECTOR_EVENT_PREVIEW_CHANGED);
    assert(tempo_selector_selected_bpm(&selector) == 90);
    assert(tempo_selector_on_rotate(&selector, -1, 2500000) ==
           TEMPO_SELECTOR_EVENT_PREVIEW_CHANGED);
    assert(tempo_selector_selected_bpm(&selector) == 140);

    assert(tempo_selector_on_encoder_click(&selector, 118, 2600000) ==
           TEMPO_SELECTOR_EVENT_CONFIRMED);
    assert(!tempo_selector_is_active(&selector));
    assert(tempo_selector_selected_bpm(&selector) == 140);
}

static void test_selection_times_out_without_confirming(void)
{
    tempo_selector_t selector;
    tempo_selector_init(&selector);

    (void)tempo_selector_on_encoder_click(&selector, 90, 3000000);
    assert(tempo_selector_on_encoder_click(&selector, 90, 3100000) ==
           TEMPO_SELECTOR_EVENT_ENTERED);
    assert(tempo_selector_poll(&selector, 8099999) == TEMPO_SELECTOR_EVENT_NONE);
    assert(tempo_selector_poll(&selector, 8100000) == TEMPO_SELECTOR_EVENT_CANCELLED);
    assert(!tempo_selector_is_active(&selector));
}

static void test_cancel_discards_delayed_click(void)
{
    tempo_selector_t selector;
    tempo_selector_init(&selector);

    (void)tempo_selector_on_encoder_click(&selector, 120, 4000000);
    tempo_selector_cancel(&selector);
    assert(tempo_selector_poll(&selector, 5000000) == TEMPO_SELECTOR_EVENT_NONE);
}

int main(void)
{
    test_single_click_becomes_transport();
    test_double_click_select_rotate_confirm();
    test_selection_times_out_without_confirming();
    test_cancel_discards_delayed_click();
    puts("PASS: tempo selector click, rotate, confirm, timeout");
    return 0;
}
