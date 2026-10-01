#include "audio_click.h"
#include "beatbox_ble.h"
#include "ble_pairing_gesture.h"
#include "board_keys.h"
#include "board_power.h"
#include "clock.h"
#include "device_mode_boot.h"
#include "device_mode_selector.h"
#include "esp_log.h"
#include "esp_system.h"
#include "esp_timer.h"
#include "freertos/FreeRTOS.h"
#include "freertos/task.h"
#include "host_link.h"
#include "led_status.h"
#include "pattern.h"
#include "sdkconfig.h"
#include "tempo.h"
#include "tempo_selector.h"

static const char *TAG = "beatbox";
static bool s_audio_ready;
static uint8_t s_last_beat_in_bar;
static uint8_t s_last_step;
static uint32_t s_last_bar;
static uint16_t s_last_tick;
static uint32_t s_quarter_count;
static bool s_prev_keys[8];
static bool s_fill_held;
/* Power-on product mode is the standalone P1 metronome. */
static bool s_drum_mode;
/* Host UI overdub: lock S7 / S8 / encoder press while armed. */
static bool s_record_armed;
static tempo_selector_t s_tempo_selector;
static device_mode_selector_t s_device_mode_selector;
static ble_pairing_gesture_t s_ble_pairing_gesture;

static void send_status(void)
{
    const uint8_t volume = s_audio_ready ? audio_click_get_volume() : 100;
    host_link_send_status(tempo_get_bpm(), tempo_is_running(), s_last_beat_in_bar, s_last_step,
                          s_last_bar, s_last_tick, s_drum_mode, volume);
}

static void apply_bpm(uint16_t bpm, const char *source)
{
    const int64_t now = esp_timer_get_time();
    tempo_set_bpm(beatbox_clamp_bpm(bpm));
    if (s_audio_ready) {
        (void)audio_click_set_bpm(tempo_get_bpm());
    }
    led_status_show_tempo(tempo_get_bpm(), now);
    send_status();
    ESP_LOGI(TAG, "BPM from %s -> %u", source, tempo_get_bpm());
}

static void apply_encoder_bpm(int8_t delta)
{
    if (delta == 0) {
        return;
    }
    int next_bpm = (int)tempo_get_bpm() + (int)delta;
    if (next_bpm < BEATBOX_BPM_MIN) {
        next_bpm = BEATBOX_BPM_MIN;
    } else if (next_bpm > BEATBOX_BPM_MAX) {
        next_bpm = BEATBOX_BPM_MAX;
    }
    apply_bpm((uint16_t)next_bpm, "encoder");
}

static void render_event(const audio_beat_event_t *event, int64_t now_us)
{
    s_last_beat_in_bar = event->beat_in_bar;
    s_last_step = event->step;
    s_last_bar = event->bar;
    s_last_tick = event->tick;

    if (event->is_quarter) {
        led_status_on_beat((uint8_t)(s_quarter_count & 0xff), event->accent, now_us);
        host_link_send_beat(event->accent, event->beat_in_bar, event->step);
        s_quarter_count++;
    }
    if (event->is_step) {
        host_link_send_position(event->bar, event->step, event->beat_in_bar, event->tick,
                                event->accent);
    }
}

static void transport_set(bool running, bool restart, bool from_host)
{
    if (running) {
        if (tempo_is_running() && !restart) {
            return;
        }
        tempo_set_running(true);
        if (restart) {
            s_last_beat_in_bar = 0;
            s_last_step = 0;
            s_last_bar = 0;
            s_last_tick = 0;
            s_quarter_count = 0;
        }
        if (s_audio_ready) {
            (void)audio_click_set_bpm(tempo_get_bpm());
            (void)audio_click_set_running(true, restart);
        }
        ESP_LOGI(TAG, "%s @ %u BPM", restart ? "PLAY" : "CONTINUE", tempo_get_bpm());
        if (!from_host) {
            if (restart) {
                host_link_send_start();
            } else {
                host_link_send_continue();
            }
        }
        send_status();
        return;
    }

    if (!tempo_is_running()) {
        return;
    }
    tempo_set_running(false);
    if (s_audio_ready) {
        (void)audio_click_stop();
    }
    if (s_audio_ready) {
        audio_click_get_position(&s_last_bar, &s_last_step, &s_last_beat_in_bar, &s_last_tick);
    }
    ESP_LOGI(TAG, "STOP");
    if (!from_host) {
        host_link_send_stop();
    }
    send_status();
}

static void toggle_transport_from_button(bool encoder_source)
{
    if (encoder_source) {
        /* Mirror the encoder click on the S8 pad in the host UI. */
        host_link_send_key(7, true);
    }
    if (tempo_is_running()) {
        transport_set(false, false, false);
    } else {
        /* Resume from saved position; Start only when already at zero. */
        const bool at_zero = s_last_bar == 0 && s_last_step == 0 && s_last_tick == 0;
        transport_set(true, at_zero, false);
    }
}

static void handle_tempo_selector_event(tempo_selector_event_t event, int64_t now_us)
{
    switch (event) {
    case TEMPO_SELECTOR_EVENT_TRANSPORT:
        toggle_transport_from_button(true);
        break;
    case TEMPO_SELECTOR_EVENT_ENTERED:
        led_status_show_tempo(tempo_selector_selected_bpm(&s_tempo_selector), now_us);
        ESP_LOGI(TAG, "Tempo preset selection entered @ %u BPM",
                 tempo_selector_selected_bpm(&s_tempo_selector));
        break;
    case TEMPO_SELECTOR_EVENT_PREVIEW_CHANGED: {
        const uint8_t index = tempo_selector_selected_index(&s_tempo_selector);
        const uint16_t bpm = tempo_selector_selected_bpm(&s_tempo_selector);
        led_status_show_tempo(bpm, now_us);
        if (s_audio_ready) {
            (void)audio_click_play_tempo_prompt((audio_tempo_prompt_t)index);
        }
        ESP_LOGI(TAG, "Tempo preset preview -> index=%u bpm=%u", (unsigned)index,
                 (unsigned)bpm);
        break;
    }
    case TEMPO_SELECTOR_EVENT_CONFIRMED:
        apply_bpm(tempo_selector_selected_bpm(&s_tempo_selector), "preset");
        ESP_LOGI(TAG, "Tempo preset confirmed");
        break;
    case TEMPO_SELECTOR_EVENT_CANCELLED:
        ESP_LOGI(TAG, "Tempo preset selection timed out; BPM unchanged");
        break;
    case TEMPO_SELECTOR_EVENT_NONE:
    default:
        break;
    }
}

static void play_device_mode_prompt(void)
{
    if (!s_audio_ready) {
        return;
    }
    const uint8_t selected = device_mode_selector_selected_index(&s_device_mode_selector);
    (void)audio_click_play_device_mode_prompt(
        selected == DEVICE_MODE_EASYINPUT ? AUDIO_DEVICE_MODE_PROMPT_EASYINPUT
                                          : AUDIO_DEVICE_MODE_PROMPT_BEATBOX);
}

static void handle_device_mode_selector_event(device_mode_selector_event_t event,
                                               int64_t now_us)
{
    switch (event) {
    case DEVICE_MODE_SELECTOR_EVENT_RELEASED_BEFORE_HOLD:
        if (!s_record_armed) {
            /*
             * A previous click may have matured while this press was held.
             * Resolve it first, then register this release as the next click.
             */
            handle_tempo_selector_event(tempo_selector_poll(&s_tempo_selector, now_us),
                                        now_us);
            handle_tempo_selector_event(
                tempo_selector_on_encoder_click(&s_tempo_selector, tempo_get_bpm(), now_us),
                now_us);
        }
        break;
    case DEVICE_MODE_SELECTOR_EVENT_ENTERED:
        tempo_selector_cancel(&s_tempo_selector);
        (void)led_status_set_solid_rgb(0, 0, 24);
        play_device_mode_prompt();
        ESP_LOGI(TAG, "Device mode selection entered; current=beatbox");
        break;
    case DEVICE_MODE_SELECTOR_EVENT_PREVIEW_CHANGED:
        (void)led_status_set_solid_rgb(
            device_mode_selector_selected_index(&s_device_mode_selector) ==
                    DEVICE_MODE_EASYINPUT
                ? 0
                : 18,
            0,
            device_mode_selector_selected_index(&s_device_mode_selector) ==
                    DEVICE_MODE_EASYINPUT
                ? 24
                : 8);
        play_device_mode_prompt();
        ESP_LOGI(TAG, "Device mode preview -> %u",
                 (unsigned)device_mode_selector_selected_index(&s_device_mode_selector));
        break;
    case DEVICE_MODE_SELECTOR_EVENT_CONFIRMED: {
        const device_mode_id_t selected =
            (device_mode_id_t)device_mode_selector_selected_index(&s_device_mode_selector);
        if (selected == DEVICE_MODE_BEATBOX) {
            (void)led_status_clear();
            ESP_LOGI(TAG, "Device mode unchanged: beatbox");
            break;
        }

        if (tempo_is_running()) {
            transport_set(false, false, false);
        }
        if (s_audio_ready) {
            (void)audio_click_stop();
        }
        const esp_err_t err = device_mode_boot_select(selected);
        if (err != ESP_OK) {
            (void)led_status_set_solid_rgb(32, 0, 0);
            ESP_LOGE(TAG, "Device mode switch rejected: %s", esp_err_to_name(err));
            break;
        }

        (void)led_status_set_solid_rgb(20, 10, 0);
        vTaskDelay(pdMS_TO_TICKS(120));
        esp_restart();
        break;
    }
    case DEVICE_MODE_SELECTOR_EVENT_CANCELLED:
        (void)led_status_clear();
        ESP_LOGI(TAG, "Device mode selection timed out; mode unchanged");
        break;
    case DEVICE_MODE_SELECTOR_EVENT_NONE:
    default:
        break;
    }
}

static void on_host_transport(bool start, bool restart)
{
    transport_set(start, restart, true);
}

static void on_host_bpm(uint16_t bpm)
{
    tempo_selector_cancel(&s_tempo_selector);
    apply_bpm(bpm, "host");
}

static void on_host_swing(uint8_t swing)
{
    pattern_set_swing(swing);
    send_status();
}

static void apply_variation(uint8_t var, const char *source)
{
    pattern_set_variation(var);
    send_status();
    ESP_LOGI(TAG, "Variation from %s -> %c", source, pattern_variation() ? 'B' : 'A');
}

static void on_host_variation(uint8_t var)
{
    apply_variation(var, "host");
}

static void on_host_fill(bool held)
{
    s_fill_held = held;
    pattern_set_fill(held);
    send_status();
}

static void on_host_note(uint8_t note, uint8_t velocity)
{
    if (s_audio_ready) {
        (void)audio_click_play_note(note, velocity ? velocity : 127);
    }
    host_link_send_note(note, velocity ? velocity : 127);
}

static void on_host_click(bool enabled)
{
    pattern_set_click(enabled);
    if (s_audio_ready) {
        (void)audio_click_set_metronome(enabled);
    }
    send_status();
}

static void on_host_mode(bool drum_mode)
{
    /* `mode` enables the drum layer; metronome click stays an independent switch. */
    s_drum_mode = drum_mode;
    if (s_audio_ready) {
        (void)audio_click_set_mode(drum_mode ? AUDIO_MODE_DRUM : AUDIO_MODE_METRONOME);
    }
    send_status();
}

static void on_host_volume(uint8_t volume)
{
    if (s_audio_ready) {
        (void)audio_click_set_volume(volume);
    }
    send_status();
}

static void on_host_pattern_set(uint8_t bank, uint32_t rev, const uint8_t *bytes)
{
    const esp_err_t err = pattern_set_bank(bank, rev, bytes);
    if (err == ESP_OK) {
        host_link_send_ack("pattern_set", true, pattern_revision());
        send_status();
    } else {
        host_link_send_error("pattern_set", "bad_bank");
        host_link_send_ack("pattern_set", false, pattern_revision());
    }
}

static void on_host_save(void)
{
    /* MVP: acknowledge save; NVS persistence is a follow-up. */
    host_link_send_ack("save", true, pattern_revision());
}

static void on_host_ping(void)
{
    host_link_send_pattern_dump();
    send_status();
}

static void on_host_record(bool armed)
{
    s_record_armed = armed;
    if (armed) {
        tempo_selector_cancel(&s_tempo_selector);
        device_mode_selector_cancel(&s_device_mode_selector);
    }
    if (armed && s_fill_held) {
        on_host_fill(false);
    }
}

static void handle_pads(const board_input_snapshot_t *in)
{
    /*
     * Hardware 4×2 performance map (finger-drumming / MPC style):
     *   S1 CHH  S2 OHH  S3 Clap S4 Rim
     *   S5 Kick S6 Snare S7 A/B|Fill S8 Play
     * Foundation Kick+Snare sit on the bottom row; hats/perc above.
     */
    static const uint8_t pad_notes[6] = {
        BEATBOX_NOTE_CHH, BEATBOX_NOTE_OHH, BEATBOX_NOTE_CLAP, BEATBOX_NOTE_RIM,
        BEATBOX_NOTE_KICK, BEATBOX_NOTE_SNARE,
    };
    static int64_t s7_down_us;
    static bool s7_armed;
    static bool s7_fill_from_hold;
    const int64_t now = esp_timer_get_time();
    const int64_t fill_hold_us = 280000;

    for (int i = 0; i < 8; ++i) {
        if (in->s[i] != s_prev_keys[i]) {
            host_link_send_key((uint8_t)i, in->s[i]);
        }
    }

    for (int i = 0; i < 6; ++i) {
        if (in->s[i] && !s_prev_keys[i]) {
            on_host_note(pad_notes[i], 127);
        }
    }

    if (s_record_armed) {
        /* Recording: ignore S7 A/B|Fill and clear any pending hold state. */
        ble_pairing_gesture_cancel(&s_ble_pairing_gesture);
        if (in->s7_released) {
            if (s7_fill_from_hold && s_fill_held) {
                on_host_fill(false);
            }
            s7_armed = false;
            s7_fill_from_hold = false;
        } else if (!in->s[6]) {
            s7_armed = false;
            s7_fill_from_hold = false;
        }
    } else if (!tempo_is_running()) {
        /* Stopped: short S7 keeps A/B; a deliberate 3 s hold opens BLE pairing. */
        if (s7_fill_from_hold && s_fill_held) {
            on_host_fill(false);
        }
        s7_armed = false;
        s7_fill_from_hold = false;
        const ble_pairing_gesture_event_t pairing_event =
            ble_pairing_gesture_update(&s_ble_pairing_gesture, in->s[6], now);
        if (pairing_event == BLE_PAIRING_GESTURE_SHORT_TAP) {
            apply_variation(pattern_variation() ? 0 : 1, "S7");
        } else if (pairing_event == BLE_PAIRING_GESTURE_OPEN_WINDOW) {
            const esp_err_t err = beatbox_ble_open_pairing_window(now);
            if (err == ESP_OK) {
                ESP_LOGI(TAG, "S7 physical pairing window opened");
            } else {
                ESP_LOGW(TAG, "S7 pairing window rejected: %s", esp_err_to_name(err));
            }
        }
    } else {
        /* Playing: preserve the original S7 hold=Fill, short tap=A/B behavior. */
        ble_pairing_gesture_cancel(&s_ble_pairing_gesture);
        if (in->s7_pressed) {
            s7_down_us = now;
            s7_armed = true;
            s7_fill_from_hold = false;
        }
        if (in->s[6] && s7_armed && !s7_fill_from_hold && (now - s7_down_us) >= fill_hold_us) {
            s7_fill_from_hold = true;
            if (!s_fill_held) {
                on_host_fill(true);
            }
        }
        if (in->s7_released) {
            if (s7_fill_from_hold) {
                if (s_fill_held) {
                    on_host_fill(false);
                }
            } else if (s7_armed) {
                apply_variation(pattern_variation() ? 0 : 1, "S7");
            }
            s7_armed = false;
            s7_fill_from_hold = false;
        }
    }

    for (int i = 0; i < 8; ++i) {
        s_prev_keys[i] = in->s[i];
    }
}

void app_main(void)
{
    ESP_ERROR_CHECK(board_keys_init());
    ESP_ERROR_CHECK(board_power_enable_peripherals());
    ESP_ERROR_CHECK(pattern_init());
    ESP_ERROR_CHECK(host_link_init());

    const host_link_handlers_t handlers = {
        .on_transport = on_host_transport,
        .on_bpm = on_host_bpm,
        .on_swing = on_host_swing,
        .on_variation = on_host_variation,
        .on_fill = on_host_fill,
        .on_note = on_host_note,
        .on_click = on_host_click,
        .on_mode = on_host_mode,
        .on_volume = on_host_volume,
        .on_pattern_set = on_host_pattern_set,
        .on_save = on_host_save,
        .on_ping = on_host_ping,
        .on_record = on_host_record,
    };
    host_link_set_handlers(&handlers);

    ESP_ERROR_CHECK(led_status_init());
    const esp_err_t audio_err = audio_click_init();
    if (audio_err != ESP_OK) {
        ESP_LOGW(TAG, "audio init failed (%s); LEDs/keys still run", esp_err_to_name(audio_err));
    } else {
        s_audio_ready = true;
    }
    ESP_ERROR_CHECK(tempo_init(120));
    tempo_selector_init(&s_tempo_selector);
    device_mode_selector_init(&s_device_mode_selector, DEVICE_MODE_COUNT);
    ble_pairing_gesture_init(&s_ble_pairing_gesture);
    if (s_audio_ready) {
        ESP_ERROR_CHECK(audio_click_set_bpm(tempo_get_bpm()));
        ESP_ERROR_CHECK(audio_click_set_metronome(pattern_click_enabled()));
        ESP_ERROR_CHECK(audio_click_set_mode(AUDIO_MODE_METRONOME));
    }

    const esp_err_t ble_err = beatbox_ble_init();
    if (ble_err != ESP_OK) {
        ESP_LOGW(TAG, "Direct BLE init failed (%s); USB fallback remains available",
                 esp_err_to_name(ble_err));
    }

    ESP_ERROR_CHECK(led_status_set_solid_rgb(0, 18, 0));
    vTaskDelay(pdMS_TO_TICKS(300));
    ESP_ERROR_CHECK(led_status_clear());

    host_link_send_hello();
    host_link_send_pattern_dump();
    send_status();
    ESP_LOGI(TAG,
             "Ready. Pads=S1-6, S7=A/B|Fill-hold, Play=S8/enc-click, "
             "Pairing=stopped-S7-hold-3s, Presets=enc-double-click, "
             "DeviceModes=enc-hold-5s");

    int64_t last_status_us = 0;
    int64_t last_hello_us = 0;
    int64_t last_led_frame_us = 0;

    while (true) {
        board_input_snapshot_t in = {0};
        ESP_ERROR_CHECK(board_keys_poll(&in));
        const int64_t input_now = esp_timer_get_time();

        if (!s_record_armed) {
            handle_device_mode_selector_event(
                device_mode_selector_poll(&s_device_mode_selector, input_now), input_now);
        }

        if (!s_record_armed && !in.enc_down &&
            !device_mode_selector_is_active(&s_device_mode_selector)) {
            handle_tempo_selector_event(tempo_selector_poll(&s_tempo_selector, input_now),
                                        input_now);
        }

        if (in.enc_delta != 0) {
            if (device_mode_selector_is_active(&s_device_mode_selector)) {
                handle_device_mode_selector_event(
                    device_mode_selector_on_rotate(&s_device_mode_selector, in.enc_delta,
                                                   input_now),
                    input_now);
            } else if (tempo_selector_is_active(&s_tempo_selector)) {
                handle_tempo_selector_event(
                    tempo_selector_on_rotate(&s_tempo_selector, in.enc_delta, input_now),
                    input_now);
            } else {
                apply_encoder_bpm(in.enc_delta);
            }
        }

        if (in.enc_pressed || in.enc_released) {
            handle_device_mode_selector_event(
                device_mode_selector_on_button(&s_device_mode_selector, in.enc_down,
                                               DEVICE_MODE_BEATBOX, input_now),
                input_now);
        }

        /* S8 remains a separate, immediate, debounced transport control. */
        if (in.s8_pressed && !s_record_armed) {
            toggle_transport_from_button(false);
        }

        handle_pads(&in);
        host_link_poll_rx();

        const int64_t now = esp_timer_get_time();
        beatbox_ble_poll(now);
        char ble_line[BEATBOX_BLE_LINE_MAX];
        while (beatbox_ble_pop_line(ble_line, sizeof(ble_line))) {
            host_link_process_line(ble_line);
        }
        char ble_diag_line[BEATBOX_BLE_DIAG_LINE_MAX];
        while (beatbox_ble_pop_diag_line(ble_diag_line, sizeof(ble_diag_line))) {
            host_link_send_usb_diagnostic(ble_diag_line);
        }

        audio_beat_event_t beat_event;
        while (s_audio_ready && audio_click_poll_beat(&beat_event)) {
            render_event(&beat_event, now);
        }

        if (now - last_led_frame_us >= 20000) {
            last_led_frame_us = now;
            if (beatbox_ble_pairing_open()) {
                (void)led_status_set_solid_rgb(0, 0, 24);
            } else {
                (void)led_status_update(now, tempo_get_bpm(), tempo_is_running());
            }
        }

        if (now - last_status_us > 500000) {
            last_status_us = now;
            if (s_audio_ready && tempo_is_running()) {
                audio_click_get_position(&s_last_bar, &s_last_step, &s_last_beat_in_bar,
                                         &s_last_tick);
            }
            send_status();
        }
        if (now - last_hello_us > 5000000) {
            last_hello_us = now;
            host_link_send_hello();
        }

        vTaskDelay(1);
    }
}
