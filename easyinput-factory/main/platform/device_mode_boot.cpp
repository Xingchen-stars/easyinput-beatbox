#include "platform/device_mode_boot.h"

#include "esp_ota_ops.h"
#include "esp_partition.h"

namespace easy_input {

esp_err_t select_device_mode_boot(ai_keyboard::DeviceMode mode) {
  const esp_partition_subtype_t subtype =
      mode == ai_keyboard::DeviceMode::Beatbox
          ? ESP_PARTITION_SUBTYPE_APP_OTA_0
          : ESP_PARTITION_SUBTYPE_APP_FACTORY;
  const char* label =
      mode == ai_keyboard::DeviceMode::Beatbox ? "beatbox" : "factory";
  const esp_partition_t* partition = esp_partition_find_first(
      ESP_PARTITION_TYPE_APP, subtype, label);
  if (partition == nullptr) {
    return ESP_ERR_NOT_FOUND;
  }
  // esp_ota_set_boot_partition validates the image header before changing
  // otadata. Selecting factory erases otadata and leaves NVS untouched.
  return esp_ota_set_boot_partition(partition);
}

}  // namespace easy_input
