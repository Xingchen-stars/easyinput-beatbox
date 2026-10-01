#pragma once

#include <cstdint>

namespace ai_keyboard {

enum class DeviceMode : std::uint8_t {
  Beatbox = 0,
  EasyInput = 1,
};

enum class DeviceModeSelectionEvent : std::uint8_t {
  None,
  PreviewChanged,
  Confirmed,
  TimedOut,
  Cancelled,
};

struct DeviceModeSelectionResult {
  bool consumed = false;
  DeviceModeSelectionEvent event = DeviceModeSelectionEvent::None;
  DeviceMode selected = DeviceMode::EasyInput;
};

// Pure two-mode selector. Hardware input, speech, boot partition changes and
// restart behavior stay in the platform layer.
class DeviceModeSelectionController {
 public:
  void arm(DeviceMode current_mode,
           std::uint32_t now_ms,
           std::uint32_t timeout_ms);
  DeviceModeSelectionResult rotate(int steps, std::uint32_t now_ms);
  DeviceModeSelectionResult confirm();
  DeviceModeSelectionResult cancel();
  DeviceModeSelectionResult update(std::uint32_t now_ms);

  bool active() const;
  DeviceMode selected() const;
  bool next_deadline_ms(std::uint32_t* deadline_ms) const;

 private:
  void refresh_deadline(std::uint32_t now_ms);
  DeviceModeSelectionResult finish(DeviceModeSelectionEvent event);

  bool active_ = false;
  DeviceMode selected_ = DeviceMode::EasyInput;
  std::uint32_t timeout_ms_ = 0;
  std::uint32_t deadline_ms_ = 0;
};

}  // namespace ai_keyboard
