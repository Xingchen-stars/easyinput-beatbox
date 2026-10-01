#include "host_link.h"

#include "beatbox_ble.h"
#include "clock.h"
#include "pattern.h"

#include "driver/usb_serial_jtag.h"
#include "driver/usb_serial_jtag_vfs.h"
#include "esp_log.h"
#include "freertos/FreeRTOS.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static const char *TAG = "host";

#define HOST_LINK_TX_BUFFER_SIZE 2048
#define HOST_LINK_RX_BUFFER_SIZE 1024
#define HOST_LINK_FRAME_SIZE      384
#define HOST_LINK_TX_RETRY_MS      10

static host_link_handlers_t s_handlers;
static bool s_ready;
static char s_rx_buf[768];
static size_t s_rx_len;

static void host_write_usb(const char *line)
{
    if (line == NULL) {
        return;
    }
    if (!s_ready) {
        return;
    }

    /*
     * Queue each JSON line as one ring-buffer item so log output cannot split
     * the protocol frame.  A purely zero-timeout write is unsafe here: ESP-IDF
     * logging uses the same USB driver and may hold the TX mutex briefly,
     * which used to make every affected state/key frame disappear.  Keep the
     * fast path non-blocking, then allow one short bounded retry.  This is long
     * enough for normal mutex contention while still keeping the board input
     * loop responsive when no browser is draining the endpoint.
     */
    char frame[HOST_LINK_FRAME_SIZE];
    const int length = snprintf(frame, sizeof(frame), "%s\n", line);
    if (length <= 0 || (size_t)length >= sizeof(frame)) {
        return;
    }
    int written = usb_serial_jtag_write_bytes(frame, (size_t)length, 0);
    if (written != length) {
        (void)usb_serial_jtag_write_bytes(frame, (size_t)length,
                                          pdMS_TO_TICKS(HOST_LINK_TX_RETRY_MS));
    }
}

static void host_write(const char *line)
{
    if (line == NULL) {
        return;
    }

    /* BLE is the daily transport; USB remains the wired recovery fallback. */
    (void)beatbox_ble_send_line(line);
    host_write_usb(line);
}

static bool extract_quoted_type(const char *line, char *out, size_t out_len)
{
    const char *t = strstr(line, "\"t\"");
    if (t == NULL) {
        return false;
    }
    const char *colon = strchr(t, ':');
    if (colon == NULL) {
        return false;
    }
    const char *q1 = strchr(colon, '"');
    if (q1 == NULL) {
        return false;
    }
    const char *q2 = strchr(q1 + 1, '"');
    if (q2 == NULL || (size_t)(q2 - (q1 + 1)) >= out_len) {
        return false;
    }
    memcpy(out, q1 + 1, (size_t)(q2 - (q1 + 1)));
    out[q2 - (q1 + 1)] = '\0';
    return true;
}

static bool extract_int_field(const char *line, const char *key, long *out)
{
    char pattern[32];
    /* Require `"key":` so short keys like "n" never match inside "note". */
    snprintf(pattern, sizeof(pattern), "\"%s\":", key);
    const char *p = strstr(line, pattern);
    if (p == NULL) {
        return false;
    }
    const char *value = p + strlen(pattern);
    char *end = NULL;
    const long parsed = strtol(value, &end, 10);
    if (end == value) {
        return false;
    }
    *out = parsed;
    return true;
}

static bool extract_quoted_field(const char *line, const char *key, char *out, size_t out_len)
{
    char pattern[32];
    snprintf(pattern, sizeof(pattern), "\"%s\"", key);
    const char *p = strstr(line, pattern);
    if (p == NULL) {
        return false;
    }
    const char *colon = strchr(p, ':');
    if (colon == NULL) {
        return false;
    }
    const char *q1 = strchr(colon, '"');
    if (q1 == NULL) {
        return false;
    }
    const char *q2 = strchr(q1 + 1, '"');
    if (q2 == NULL || (size_t)(q2 - (q1 + 1)) >= out_len) {
        return false;
    }
    memcpy(out, q1 + 1, (size_t)(q2 - (q1 + 1)));
    out[q2 - (q1 + 1)] = '\0';
    return true;
}

esp_err_t host_link_init(void)
{
    if (!usb_serial_jtag_is_driver_installed()) {
        usb_serial_jtag_driver_config_t config = {
            .tx_buffer_size = HOST_LINK_TX_BUFFER_SIZE,
            .rx_buffer_size = HOST_LINK_RX_BUFFER_SIZE,
        };
        const esp_err_t error = usb_serial_jtag_driver_install(&config);
        if (error != ESP_OK) {
            return error;
        }
    }
    usb_serial_jtag_vfs_use_driver();

    s_ready = true;
    s_rx_len = 0;
    ESP_LOGI(TAG, "USB Serial fallback ready (shared protocol v3)");
    host_link_send_hello();
    return ESP_OK;
}

bool host_link_ready(void)
{
    return s_ready;
}

void host_link_send_hello(void)
{
    host_write(
        "{\"t\":\"hello\",\"v\":3,\"name\":\"EasyInput Beatbox\",\"caps\":[\"drum\",\"pattern\","
        "\"swing\",\"volume\",\"record\",\"ble_direct\"]}");
}

void host_link_send_start(void)
{
    host_write("{\"t\":\"start\"}");
}

void host_link_send_continue(void)
{
    host_write("{\"t\":\"continue\"}");
}

void host_link_send_stop(void)
{
    host_write("{\"t\":\"stop\"}");
}

void host_link_send_beat(bool accent, uint8_t beat_in_bar, uint8_t step)
{
    char line[128];
    snprintf(line, sizeof(line),
             "{\"t\":\"beat\",\"accent\":%d,\"beat\":%u,\"step\":%u}", accent ? 1 : 0,
             (unsigned)(beat_in_bar & 0x03), (unsigned)(step & 0x0f));
    host_write(line);
}

void host_link_send_position(uint32_t bar, uint8_t step, uint8_t beat, uint16_t tick,
                             bool accent)
{
    char line[160];
    snprintf(line, sizeof(line),
             "{\"t\":\"position\",\"bar\":%lu,\"step\":%u,\"beat\":%u,\"tick\":%u,\"accent\":%d}",
             (unsigned long)bar, (unsigned)(step & 0x0f), (unsigned)(beat & 0x03),
             (unsigned)tick, accent ? 1 : 0);
    host_write(line);
}

void host_link_send_note(uint8_t note, uint8_t velocity)
{
    char line[96];
    snprintf(line, sizeof(line), "{\"t\":\"note\",\"n\":%u,\"v\":%u}", (unsigned)note,
             (unsigned)velocity);
    host_write(line);
}

void host_link_send_key(uint8_t index, bool down)
{
    char line[72];
    snprintf(line, sizeof(line), "{\"t\":\"key\",\"i\":%u,\"v\":%d}", (unsigned)(index & 0x07),
             down ? 1 : 0);
    host_write(line);
}

void host_link_send_status(uint16_t bpm, bool running, uint8_t beat_in_bar, uint8_t step,
                           uint32_t bar, uint16_t tick, bool drum_mode, uint8_t volume)
{
    char line[288];
    snprintf(line, sizeof(line),
             "{\"t\":\"state\",\"bpm\":%u,\"run\":%d,\"beat\":%u,\"step\":%u,\"bar\":%lu,"
             "\"tick\":%u,\"swing\":%u,\"var\":%u,\"fill\":%d,\"rev\":%lu,\"click\":%d,"
             "\"mode\":%u,\"vol\":%u}",
             (unsigned)bpm, running ? 1 : 0, (unsigned)(beat_in_bar & 0x03),
             (unsigned)(step & 0x0f), (unsigned long)bar, (unsigned)tick,
             (unsigned)pattern_swing(), (unsigned)pattern_variation(),
             pattern_fill_active() ? 1 : 0, (unsigned long)pattern_revision(),
             pattern_click_enabled() ? 1 : 0, drum_mode ? 1u : 0u,
             (unsigned)(volume > BEATBOX_VOLUME_MAX ? BEATBOX_VOLUME_MAX : volume));
    host_write(line);
}

void host_link_send_pattern_dump(void)
{
    /* One bank per line — long composite dumps get truncated on USB-Serial/JTAG. */
    char hex[BEATBOX_PATTERN_HEX_CHARS + 1];
    char line[280];
    for (uint8_t bank = 0; bank < BEATBOX_BANK_COUNT; ++bank) {
        pattern_encode_hex(pattern_bank(bank), hex, sizeof(hex));
        snprintf(line, sizeof(line),
                 "{\"t\":\"pattern\",\"bank\":%u,\"rev\":%lu,\"p\":\"%s\"}", (unsigned)bank,
                 (unsigned long)pattern_revision(), hex);
        host_write(line);
    }
}

void host_link_send_ack(const char *cmd, bool ok, uint32_t rev)
{
    char line[128];
    snprintf(line, sizeof(line), "{\"t\":\"ack\",\"cmd\":\"%s\",\"ok\":%d,\"rev\":%lu}",
             cmd ? cmd : "", ok ? 1 : 0, (unsigned long)rev);
    host_write(line);
}

void host_link_send_error(const char *cmd, const char *msg)
{
    char line[192];
    snprintf(line, sizeof(line), "{\"t\":\"error\",\"cmd\":\"%s\",\"msg\":\"%s\"}",
             cmd ? cmd : "", msg ? msg : "");
    host_write(line);
}

void host_link_send_usb_diagnostic(const char *line)
{
    host_write_usb(line);
}

void host_link_set_handlers(const host_link_handlers_t *handlers)
{
    if (handlers) {
        s_handlers = *handlers;
    } else {
        memset(&s_handlers, 0, sizeof(s_handlers));
    }
}

static void handle_line(const char *line)
{
    char type[32];
    if (!extract_quoted_type(line, type, sizeof(type))) {
        return;
    }

    if (strcmp(type, "start") == 0) {
        if (s_handlers.on_transport) {
            s_handlers.on_transport(true, true);
        }
        return;
    }
    if (strcmp(type, "continue") == 0) {
        if (s_handlers.on_transport) {
            s_handlers.on_transport(true, false);
        }
        return;
    }
    if (strcmp(type, "stop") == 0) {
        if (s_handlers.on_transport) {
            s_handlers.on_transport(false, false);
        }
        return;
    }
    if (strcmp(type, "ping") == 0) {
        host_link_send_hello();
        if (s_handlers.on_ping) {
            s_handlers.on_ping();
        } else {
            host_link_send_pattern_dump();
        }
        return;
    }
    if (strcmp(type, "bpm") == 0 && s_handlers.on_bpm) {
        long bpm = 120;
        if (extract_int_field(line, "v", &bpm)) {
            s_handlers.on_bpm((uint16_t)bpm);
        }
        return;
    }
    if (strcmp(type, "swing") == 0 && s_handlers.on_swing) {
        long swing = 50;
        if (extract_int_field(line, "v", &swing)) {
            s_handlers.on_swing((uint8_t)swing);
        }
        return;
    }
    if (strcmp(type, "variation") == 0 && s_handlers.on_variation) {
        long var = 0;
        if (extract_int_field(line, "v", &var)) {
            s_handlers.on_variation((uint8_t)var);
        }
        return;
    }
    if (strcmp(type, "fill") == 0 && s_handlers.on_fill) {
        long fill = 0;
        if (extract_int_field(line, "v", &fill)) {
            s_handlers.on_fill(fill != 0);
        }
        return;
    }
    if (strcmp(type, "click") == 0 && s_handlers.on_click) {
        long click = 1;
        if (extract_int_field(line, "v", &click)) {
            s_handlers.on_click(click != 0);
        }
        return;
    }
    if (strcmp(type, "mode") == 0 && s_handlers.on_mode) {
        long mode = 0;
        if (extract_int_field(line, "v", &mode)) {
            s_handlers.on_mode(mode != 0);
        }
        return;
    }
    if (strcmp(type, "volume") == 0 && s_handlers.on_volume) {
        long volume = 100;
        if (extract_int_field(line, "v", &volume)) {
            if (volume < 0) {
                volume = 0;
            }
            if (volume > BEATBOX_VOLUME_MAX) {
                volume = BEATBOX_VOLUME_MAX;
            }
            s_handlers.on_volume((uint8_t)volume);
        }
        return;
    }
    if (strcmp(type, "note") == 0 && s_handlers.on_note) {
        long n = 0;
        long v = 127;
        if (extract_int_field(line, "n", &n)) {
            (void)extract_int_field(line, "v", &v);
            s_handlers.on_note((uint8_t)n, (uint8_t)v);
        }
        return;
    }
    if (strcmp(type, "pattern_get") == 0) {
        host_link_send_pattern_dump();
        return;
    }
    if (strcmp(type, "pattern_set") == 0 && s_handlers.on_pattern_set) {
        long bank = 0;
        long rev = 0;
        char hex[BEATBOX_PATTERN_HEX_CHARS + 1];
        if (!extract_int_field(line, "bank", &bank) || !extract_int_field(line, "rev", &rev) ||
            !extract_quoted_field(line, "p", hex, sizeof(hex))) {
            host_link_send_error("pattern_set", "bad_args");
            return;
        }
        uint8_t bytes[BEATBOX_PATTERN_BYTES];
        if (pattern_decode_hex(hex, bytes) != ESP_OK) {
            host_link_send_error("pattern_set", "bad_hex");
            return;
        }
        s_handlers.on_pattern_set((uint8_t)bank, (uint32_t)rev, bytes);
        return;
    }
    if (strcmp(type, "save") == 0 && s_handlers.on_save) {
        s_handlers.on_save();
        return;
    }
    if (strcmp(type, "record") == 0 && s_handlers.on_record) {
        long armed = 0;
        if (extract_int_field(line, "v", &armed)) {
            s_handlers.on_record(armed != 0);
        }
        return;
    }
}

void host_link_process_line(const char *line)
{
    if (line != NULL && line[0] != '\0') {
        handle_line(line);
    }
}

void host_link_poll_rx(void)
{
    if (!s_ready) {
        return;
    }

    char chunk[128];
    while (true) {
        const int n = usb_serial_jtag_read_bytes(chunk, sizeof(chunk), 0);
        if (n <= 0) {
            break;
        }

        for (int i = 0; i < n; ++i) {
            const char c = chunk[i];
            if (c == '\r') {
                continue;
            }
            if (c == '\n') {
                if (s_rx_len > 0) {
                    s_rx_buf[s_rx_len] = '\0';
                    host_link_process_line(s_rx_buf);
                    s_rx_len = 0;
                }
                continue;
            }
            if (s_rx_len + 1 < sizeof(s_rx_buf)) {
                s_rx_buf[s_rx_len++] = c;
            } else {
                s_rx_len = 0;
            }
        }
    }

    (void)TAG;
}
