#pragma once

#include "esp_err.h"
#include "keyboard/device_mode_selection.h"

namespace easy_input {

// Validates the target image and changes only the boot partition selection.
// The caller owns transport shutdown and the final esp_restart().
esp_err_t select_device_mode_boot(ai_keyboard::DeviceMode mode);

}  // namespace easy_input
