#include <cassert>
#include <cstdint>

#include "keyboard/device_mode_selection.h"

int main() {
  using ai_keyboard::DeviceMode;
  using ai_keyboard::DeviceModeSelectionController;
  using ai_keyboard::DeviceModeSelectionEvent;

  DeviceModeSelectionController selector;
  assert(!selector.active());

  selector.arm(DeviceMode::EasyInput, 100U, 10000U);
  assert(selector.active());
  assert(selector.selected() == DeviceMode::EasyInput);
  std::uint32_t deadline_ms = 0U;
  assert(selector.next_deadline_ms(&deadline_ms));
  assert(deadline_ms == 10100U);

  auto result = selector.rotate(1, 200U);
  assert(result.consumed);
  assert(result.event == DeviceModeSelectionEvent::PreviewChanged);
  assert(result.selected == DeviceMode::Beatbox);

  result = selector.rotate(-1, 300U);
  assert(result.event == DeviceModeSelectionEvent::PreviewChanged);
  assert(result.selected == DeviceMode::EasyInput);

  result = selector.rotate(-3, 400U);
  assert(result.event == DeviceModeSelectionEvent::PreviewChanged);
  assert(result.selected == DeviceMode::Beatbox);

  result = selector.confirm();
  assert(result.consumed);
  assert(result.event == DeviceModeSelectionEvent::Confirmed);
  assert(result.selected == DeviceMode::Beatbox);
  assert(!selector.active());
  assert(!selector.next_deadline_ms(&deadline_ms));

  selector.arm(DeviceMode::EasyInput, 0xFFFFFF00U, 1000U);
  assert(selector.update(0x000002E7U).event ==
         DeviceModeSelectionEvent::None);
  result = selector.update(0x000002E8U);
  assert(result.event == DeviceModeSelectionEvent::TimedOut);
  assert(!selector.active());

  selector.arm(DeviceMode::EasyInput, 10U, 1000U);
  result = selector.cancel();
  assert(result.event == DeviceModeSelectionEvent::Cancelled);
  assert(!selector.active());
  assert(!selector.cancel().consumed);
  return 0;
}
