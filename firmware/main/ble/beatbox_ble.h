#pragma once

#include <stdbool.h>
#include <stddef.h>

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

#define BEATBOX_BLE_PAIRING_WINDOW_US 60000000LL
#define BEATBOX_BLE_LINE_MAX 768
#define BEATBOX_BLE_DIAG_LINE_MAX 256

/** Initialize the browser-authorized Beatbox GATT transport. */
esp_err_t beatbox_ble_init(void);

/** Open the physical-presence enrollment window for one new computer. */
esp_err_t beatbox_ble_open_pairing_window(int64_t now_us);

/** Expire timers and move complete BLE frames into the application loop. */
void beatbox_ble_poll(int64_t now_us);

bool beatbox_ble_pairing_open(void);
bool beatbox_ble_authorized_connected(void);

/** Pop one complete host-protocol JSON line. */
bool beatbox_ble_pop_line(char *out, size_t out_len);

/**
 * Pop one USB-only diagnostic JSON line produced by the NimBLE host task.
 * Diagnostics never change pairing state and are not sent back over BLE.
 */
bool beatbox_ble_pop_diag_line(char *out, size_t out_len);

/** Best-effort notification of one host-protocol JSON line. */
void beatbox_ble_send_line(const char *line);

#ifdef __cplusplus
}
#endif
