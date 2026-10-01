#include "keyboard/device_mode_selection.h"

namespace ai_keyboard {
namespace {

constexpr int kDeviceModeCount = 2;

int mode_index(DeviceMode mode) {
  return mode == DeviceMode::Beatbox ? 0 : 1;
}

DeviceMode mode_from_index(int index) {
  return index == 0 ? DeviceMode::Beatbox : DeviceMode::EasyInput;
}

}  // namespace

void DeviceModeSelectionController::arm(
    DeviceMode current_mode,
    std::uint32_t now_ms,
    std::uint32_t timeout_ms) {
  active_ = true;
  selected_ = current_mode;
  timeout_ms_ = timeout_ms;
  refresh_deadline(now_ms);
}

DeviceModeSelectionResult DeviceModeSelectionController::rotate(
    int steps,
    std::uint32_t now_ms) {
  if (!active_) {
    return {};
  }
  DeviceModeSelectionResult result;
  result.consumed = true;
  result.selected = selected_;
  if (steps == 0) {
    return result;
  }
  int index = mode_index(selected_);
  index = (index + (steps % kDeviceModeCount)) % kDeviceModeCount;
  if (index < 0) {
    index += kDeviceModeCount;
  }
  const auto next = mode_from_index(index);
  if (next != selected_) {
    selected_ = next;
    result.event = DeviceModeSelectionEvent::PreviewChanged;
    result.selected = selected_;
  }
  refresh_deadline(now_ms);
  return result;
}

DeviceModeSelectionResult DeviceModeSelectionController::confirm() {
  return active_ ? finish(DeviceModeSelectionEvent::Confirmed)
                 : DeviceModeSelectionResult{};
}

DeviceModeSelectionResult DeviceModeSelectionController::cancel() {
  return active_ ? finish(DeviceModeSelectionEvent::Cancelled)
                 : DeviceModeSelectionResult{};
}

DeviceModeSelectionResult DeviceModeSelectionController::update(
    std::uint32_t now_ms) {
  if (!active_ || timeout_ms_ == 0U ||
      static_cast<std::int32_t>(now_ms - deadline_ms_) < 0) {
    return {};
  }
  return finish(DeviceModeSelectionEvent::TimedOut);
}

bool DeviceModeSelectionController::active() const {
  return active_;
}

DeviceMode DeviceModeSelectionController::selected() const {
  return selected_;
}

bool DeviceModeSelectionController::next_deadline_ms(
    std::uint32_t* deadline_ms) const {
  if (!active_ || timeout_ms_ == 0U || deadline_ms == nullptr) {
    return false;
  }
  *deadline_ms = deadline_ms_;
  return true;
}

void DeviceModeSelectionController::refresh_deadline(std::uint32_t now_ms) {
  deadline_ms_ = now_ms + timeout_ms_;
}

DeviceModeSelectionResult DeviceModeSelectionController::finish(
    DeviceModeSelectionEvent event) {
  DeviceModeSelectionResult result;
  result.consumed = true;
  result.event = event;
  result.selected = selected_;
  active_ = false;
  timeout_ms_ = 0;
  deadline_ms_ = 0;
  return result;
}

}  // namespace ai_keyboard
