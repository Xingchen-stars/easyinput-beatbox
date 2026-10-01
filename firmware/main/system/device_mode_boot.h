#pragma once

#include "esp_err.h"

#ifdef __cplusplus
extern "C" {
#endif

typedef enum {
    DEVICE_MODE_BEATBOX = 0,
    DEVICE_MODE_EASYINPUT,
    DEVICE_MODE_COUNT,
} device_mode_id_t;

/**
 * Select the complete application image that should start after reboot.
 *
 * DEVICE_MODE_EASYINPUT maps to the preserved factory partition; Beatbox maps
 * to the dedicated ota_0 partition. esp_ota_set_boot_partition validates the
 * target image before changing otadata, so an absent/corrupt peer image does
 * not strand the board.
 */
esp_err_t device_mode_boot_select(device_mode_id_t mode);

const char *device_mode_boot_partition_label(device_mode_id_t mode);

#ifdef __cplusplus
}
#endif
