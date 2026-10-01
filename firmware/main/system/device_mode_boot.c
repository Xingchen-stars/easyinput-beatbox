#include "device_mode_boot.h"

#include "esp_log.h"
#include "esp_ota_ops.h"
#include "esp_partition.h"

static const char *TAG = "device_mode_boot";

const char *device_mode_boot_partition_label(device_mode_id_t mode)
{
    switch (mode) {
    case DEVICE_MODE_BEATBOX:
        return "beatbox";
    case DEVICE_MODE_EASYINPUT:
        return "factory";
    case DEVICE_MODE_COUNT:
    default:
        return NULL;
    }
}

esp_err_t device_mode_boot_select(device_mode_id_t mode)
{
    const char *label = device_mode_boot_partition_label(mode);
    if (label == NULL) {
        return ESP_ERR_INVALID_ARG;
    }

    const esp_partition_subtype_t subtype =
        mode == DEVICE_MODE_EASYINPUT ? ESP_PARTITION_SUBTYPE_APP_FACTORY
                                      : ESP_PARTITION_SUBTYPE_APP_OTA_0;
    const esp_partition_t *partition =
        esp_partition_find_first(ESP_PARTITION_TYPE_APP, subtype, label);
    if (partition == NULL) {
        ESP_LOGE(TAG, "target partition missing mode=%u label=%s", (unsigned)mode, label);
        return ESP_ERR_NOT_FOUND;
    }

    const esp_err_t err = esp_ota_set_boot_partition(partition);
    if (err != ESP_OK) {
        ESP_LOGE(TAG, "target image rejected mode=%u label=%s err=%s", (unsigned)mode,
                 label, esp_err_to_name(err));
        return err;
    }
    ESP_LOGI(TAG, "next boot mode=%u partition=%s offset=0x%lx", (unsigned)mode, label,
             (unsigned long)partition->address);
    return ESP_OK;
}
