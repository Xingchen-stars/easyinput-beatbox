#include <cassert>
#include <array>
#include <algorithm>
#include <cstdio>
#include <string>
#include <vector>

#include "keyboard/config_state.h"
#include "keyboard/host_action_protocol.h"
#include "keyboard/hid_report_queue.h"
#include "keyboard/status_hid_protocol.h"

namespace {

// Synthetic test identity only; never a default or a real App mapping.
constexpr const char* kUuid = "01234567-89ab-cdef-0123-456789abcdef";

std::string payload_with_action(const std::string& action) {
  return std::string(R"({"schema":"ai_keyboard.v1","profiles":[{
    "id":"default","keys":{
      "KEY1":{"press":")") + action + R"("},
      "KEY2":{"press":"disabled"},"KEY3":{"press":"disabled"},
      "KEY4":{"press":"disabled"},"KEY5":{"press":"disabled"},
      "KEY6":{"press":"disabled"},"KEY7":{"press":"disabled"},
      "KEY8":{"press":"disabled"}},
    "encoder":{"left":"disabled","right":"disabled","press":"disabled"}
  }]})";
}

void valid_config_is_accepted_without_rewriting() {
  const auto json = payload_with_action(std::string("host_action:") + kUuid);
  ai_keyboard::ConfigState state;
  assert(state.apply_json(json) == ai_keyboard::ConfigParseStatus::Ok);
  assert(state.last_applied_json() == json);
  const auto& action = state.keymap().action_for(ai_keyboard::InputId::Key1);
  assert(action.kind == ai_keyboard::ActionKind::HostAction);
  assert(action.host_action == std::string("host_action:") + kUuid);
  assert(action.hotkey.empty());
  assert(action.text.empty());
  ai_keyboard::ConfigState restored;
  assert(restored.apply_json(state.last_applied_json()) ==
         ai_keyboard::ConfigParseStatus::Ok);
  assert(restored.keymap().action_for(ai_keyboard::InputId::Key1).host_action ==
         action.host_action);
}

std::vector<std::string> invalid_uuids() {
  const std::string uuid(kUuid);
  std::vector<std::string> invalid{
      "", uuid.substr(1), uuid + "0", "0123456789abcdef0123456789abcdef",
      " " + uuid, uuid + " ", uuid + std::string(1, '\0')};
  for (std::size_t index = 0; index < uuid.size(); ++index) {
    auto changed = uuid;
    changed[index] = uuid[index] == '-' ? '0' : '-';
    invalid.push_back(changed);
    if (uuid[index] >= 'a' && uuid[index] <= 'f') {
      changed = uuid;
      changed[index] -= 'a' - 'A';
      invalid.push_back(changed);
    }
  }
  for (const char bad : {'g', '/', ':', ' ', '\0', static_cast<char>(0x80)}) {
    auto changed = uuid;
    changed[0] = bad;
    invalid.push_back(changed);
  }
  return invalid;
}

void canonical_uuid_validation_is_exact_and_has_no_version_or_nil_gate() {
  for (const std::string uuid : {std::string(kUuid),
                                std::string("00000000-0000-0000-0000-000000000000"),
                                std::string("ffffffff-ffff-ffff-ffff-ffffffffffff")}) {
    assert(ai_keyboard::is_canonical_lowercase_uuid(uuid));
    assert(ai_keyboard::is_host_action_config("host_action:" + uuid));
  }
  for (const auto& uuid : invalid_uuids()) {
    assert(!ai_keyboard::is_canonical_lowercase_uuid(uuid));
    assert(!ai_keyboard::is_host_action_config("host_action:" + uuid));
  }
}

void invalid_config_preserves_previous_state_and_is_not_normalized() {
  ai_keyboard::ConfigState state;
  const std::string config = std::string("host_action:") + kUuid;
  const auto json = payload_with_action(config);
  assert(state.apply_json(json) == ai_keyboard::ConfigParseStatus::Ok);
  auto invalid = invalid_uuids();
  for (auto& uuid : invalid) {
    uuid.insert(0, "host_action:");
  }
  for (const auto* prefix : {"", "Host_action:", "host action:", "host_action："}) {
    invalid.push_back(std::string(prefix) + kUuid);
  }
  for (const auto& value : invalid) {
    assert(state.apply_json(payload_with_action(value)) !=
           ai_keyboard::ConfigParseStatus::Ok);
    assert(state.last_applied_json() == json);
    assert(state.keymap().action_for(ai_keyboard::InputId::Key1).host_action == config);
  }
}

void press_emits_one_host_action_and_release_emits_none() {
  ai_keyboard::ConfigState state;
  const std::string config = std::string("host_action:") + kUuid;
  assert(state.apply_json(payload_with_action(config)) == ai_keyboard::ConfigParseStatus::Ok);
  const auto& action = state.keymap().action_for(ai_keyboard::InputId::Key1);
  unsigned events = 0;
  for (const auto phase : {ai_keyboard::InputPhase::Pressed,
                           ai_keyboard::InputPhase::Released}) {
    const auto event = ai_keyboard::event_for_action(action, phase, "F12", "F13");
    if (phase == ai_keyboard::InputPhase::Pressed) {
      assert(event.kind == ai_keyboard::FirmwareEventKind::HostAction);
      assert(event.value == config);
      assert(!event.bridge_app_hotkey);
      ++events;
    } else {
      assert(event.kind == ai_keyboard::FirmwareEventKind::None);
      assert(event.value.empty());
    }
  }
  assert(events == 1);
  // Bypassing config parsing must still fail closed at event generation.
  for (const auto& uuid : invalid_uuids()) {
    auto invalid = action;
    invalid.host_action = "host_action:" + uuid;
    assert(ai_keyboard::event_for_action(invalid, ai_keyboard::InputPhase::Pressed,
                                         "F12", "F13").kind ==
           ai_keyboard::FirmwareEventKind::None);
  }
}

void encoding_has_exact_fields_no_prefix_and_zero_tail() {
  assert(ai_keyboard::kHostActionReportId == 0x11);
  assert(ai_keyboard::kHostActionCommandKind == 0x05);
  assert(ai_keyboard::kStatusResponseCommandKind == 0x04);
  const std::string config = std::string("host_action:") + kUuid;
  ai_keyboard::HostActionReport report;
  report.fill(0xa5);
  assert(ai_keyboard::encode_host_action_report(config, &report));
  std::array<std::uint8_t, 63> expected{};
  expected[0] = 0x05;
  expected[1] = 0;
  expected[2] = 1;
  expected[3] = 36;
  std::copy_n(kUuid, 36, expected.begin() + 4);
  assert(report == expected);
  assert(std::string(report.begin() + 4, report.begin() + 40) == kUuid);
  assert(std::all_of(report.begin() + 40, report.end(), [](auto b) { return b == 0; }));
  assert(!ai_keyboard::encode_host_action_report(config, nullptr));
  auto invalid = invalid_uuids();
  for (auto& uuid : invalid) {
    uuid.insert(0, "host_action:");
  }
  invalid.emplace_back(kUuid);  // Encoding accepts a full config, not an untyped UUID.
  for (const auto& value : invalid) {
    report.fill(0xa5);
    assert(!ai_keyboard::encode_host_action_report(value, &report));
    assert(std::all_of(report.begin(), report.end(), [](auto b) { return b == 0; }));
  }
}

constexpr std::array<std::pair<const char*, ai_keyboard::InputId>, 8> kMainKeys{{
    {"KEY1", ai_keyboard::InputId::Key1}, {"KEY2", ai_keyboard::InputId::Key2},
    {"KEY3", ai_keyboard::InputId::Key3}, {"KEY4", ai_keyboard::InputId::Key4},
    {"KEY5", ai_keyboard::InputId::Key5}, {"KEY6", ai_keyboard::InputId::Key6},
    {"KEY7", ai_keyboard::InputId::Key7}, {"KEY8", ai_keyboard::InputId::Key8},
}};

std::string host_action_for_key(std::size_t index) {
  // Distinct synthetic identities catch wrong-slot copies; test data only.
  std::string uuid(kUuid);
  uuid.back() = static_cast<char>('1' + index);
  return "host_action:" + uuid;
}

std::string payload_with_key_actions(const std::array<std::string, 8>& actions) {
  std::string json = R"({"schema":"ai_keyboard.v1","profiles":[{"id":"default","keys":{)";
  for (std::size_t index = 0; index < kMainKeys.size(); ++index) {
    if (index != 0) json += ',';
    json += std::string("\"") + kMainKeys[index].first + "\":{\"press\":" +
            actions[index] + '}';
  }
  return json + R"(},"encoder":{"left":"disabled","right":"disabled","press":"disabled"}}]})";
}

void assert_host_action_cycle(const ai_keyboard::Action& action,
                              const std::string& config,
                              ai_keyboard::HostPlatform platform) {
  assert(action.kind == ai_keyboard::ActionKind::HostAction);
  assert(action.host_action == config);
  assert(action.hotkey.empty() && action.text.empty());
  unsigned generated = 0;
  for (const auto phase : {ai_keyboard::InputPhase::Pressed,
                           ai_keyboard::InputPhase::Released}) {
    const auto event = ai_keyboard::event_for_action(action, phase, "F12", "F13", platform);
    if (event.kind != ai_keyboard::FirmwareEventKind::None) ++generated;
    assert(event.kind == (phase == ai_keyboard::InputPhase::Pressed
                             ? ai_keyboard::FirmwareEventKind::HostAction
                             : ai_keyboard::FirmwareEventKind::None));
    assert(event.value == (phase == ai_keyboard::InputPhase::Pressed ? config : ""));
    assert(!event.bridge_app_hotkey);
  }
  assert(generated == 1);
}

void all_main_keys_preserve_distinct_host_actions_and_events() {
  std::array<std::string, 8> actions;
  for (std::size_t index = 0; index < actions.size(); ++index) {
    actions[index] = '"' + host_action_for_key(index) + '"';
  }
  const auto json = payload_with_key_actions(actions);
  ai_keyboard::ConfigState state;
  assert(state.apply_json(json) == ai_keyboard::ConfigParseStatus::Ok);
  assert(state.last_applied_json() == json);
  ai_keyboard::ConfigState restored;
  assert(restored.apply_json(state.last_applied_json()) == ai_keyboard::ConfigParseStatus::Ok);
  assert(restored.last_applied_json() == json);
  for (std::size_t index = 0; index < kMainKeys.size(); ++index) {
    for (const auto* config : {&state, &restored}) {
      for (const auto platform : {ai_keyboard::HostPlatform::MacOS,
                                  ai_keyboard::HostPlatform::Windows}) {
        assert_host_action_cycle(config->keymap().action_for(kMainKeys[index].second),
                                 host_action_for_key(index), platform);
      }
    }
    std::printf("%s: full config + restore + press=1/release=0 PASS\n", kMainKeys[index].first);
  }
}

void all_main_keys_mix_host_actions_with_legacy_actions() {
  struct LegacyCase {
    const char* name;
    const char* json;
    ai_keyboard::ActionKind kind;
    const char* macos_value;
    const char* windows_value;
  };
  const std::array<LegacyCase, 4> legacy{{
      {"copy", R"("copy")", ai_keyboard::ActionKind::Copy, "Meta+C", "Ctrl+C"},
      {"paste", R"("paste")", ai_keyboard::ActionKind::Paste, "Meta+V", "Ctrl+V"},
      {"hotkey", R"({"hotkey":"Ctrl+Shift+K"})", ai_keyboard::ActionKind::Hotkey,
       "Ctrl+Shift+K", "Ctrl+Shift+K"},
      {"fixed_text", R"({"text":"T04 sample text"})", ai_keyboard::ActionKind::FixedText,
       "T04 sample text", "T04 sample text"},
  }};
  for (const auto& previous : legacy) {
    for (std::size_t target = 0; target < kMainKeys.size(); ++target) {
      std::array<std::string, 8> actions;
      actions.fill(previous.json);
      actions[target] = '"' + host_action_for_key(target) + '"';
      const auto json = payload_with_key_actions(actions);
      ai_keyboard::ConfigState state;
      assert(state.apply_json(json) == ai_keyboard::ConfigParseStatus::Ok);
      assert(state.last_applied_json() == json);
      for (std::size_t index = 0; index < kMainKeys.size(); ++index) {
        const auto& action = state.keymap().action_for(kMainKeys[index].second);
        for (const auto platform : {ai_keyboard::HostPlatform::MacOS,
                                    ai_keyboard::HostPlatform::Windows}) {
          if (index == target) {
            assert_host_action_cycle(action, host_action_for_key(target), platform);
            continue;
          }
          assert(action.kind == previous.kind);
          assert(action.host_action.empty());
          const std::string expected = platform == ai_keyboard::HostPlatform::MacOS
                                           ? previous.macos_value : previous.windows_value;
          const bool text = previous.kind == ai_keyboard::ActionKind::FixedText;
          const bool hotkey = previous.kind == ai_keyboard::ActionKind::Hotkey;
          assert(action.hotkey == (hotkey ? expected : ""));
          assert(action.text == (text ? expected : ""));
          const auto press = ai_keyboard::event_for_action(
              action, ai_keyboard::InputPhase::Pressed, "F12", "F13", platform);
          const auto release = ai_keyboard::event_for_action(
              action, ai_keyboard::InputPhase::Released, "F12", "F13", platform);
          assert(press.kind == (text ? ai_keyboard::FirmwareEventKind::FixedText
                                    : ai_keyboard::FirmwareEventKind::HidKeyDown));
          assert(press.value == expected);
          assert(release.kind == (text ? ai_keyboard::FirmwareEventKind::None
                                      : ai_keyboard::FirmwareEventKind::HidKeyUp));
          assert(release.value == (text ? "" : expected));
          assert(!press.bridge_app_hotkey && !release.bridge_app_hotkey);
        }
      }
      std::printf("%s: host action + %s coexistence PASS\n", kMainKeys[target].first, previous.name);
    }
  }
}

void default_keymap_does_not_embed_host_action_test_identities() {
  std::vector<std::string> test_uuids{kUuid};
  for (std::size_t index = 0; index < kMainKeys.size(); ++index) {
    test_uuids.push_back(host_action_for_key(index).substr(ai_keyboard::kHostActionPrefix.size()));
  }
  const auto defaults = ai_keyboard::DefaultKeymap();
  for (std::size_t index = 0; index < static_cast<std::size_t>(ai_keyboard::InputId::Count); ++index) {
    const auto& action = defaults.action_for(static_cast<ai_keyboard::InputId>(index));
    assert(action.kind != ai_keyboard::ActionKind::HostAction);
    assert(action.host_action.empty());
    // A bare UUID substring also catches its full host_action: form.
    for (const auto& uuid : test_uuids) {
      assert(action.hotkey.find(uuid) == std::string::npos);
      assert(action.text.find(uuid) == std::string::npos);
    }
  }
}

void host_action_payload_survives_both_existing_queue_forms() {
  ai_keyboard::ConfigState state;
  const std::string config = std::string("host_action:") + kUuid;
  assert(state.apply_json(payload_with_action(config)) == ai_keyboard::ConfigParseStatus::Ok);
  const auto event = ai_keyboard::event_for_action(
      state.keymap().action_for(ai_keyboard::InputId::Key1),
      ai_keyboard::InputPhase::Pressed, "F12", "F13");
  assert(event.kind == ai_keyboard::FirmwareEventKind::HostAction);
  ai_keyboard::HostActionReport report{};
  assert(ai_keyboard::encode_host_action_report(event.value, &report));

  // Separate adapter scenarios, not a production dual-send. Keep the exact
  // shared payload, USB epoch and BLE owner through their existing queue APIs.
  ai_keyboard::HidReportQueue usb;
  ai_keyboard::HidReportQueue ble;
  constexpr std::uint32_t epoch = 11;
  const ai_keyboard::BleOwnerToken owner{7, 21};
  const auto push_usb = [&] {
    return usb.push(ai_keyboard::kHostActionReportId, report.data(), report.size(),
                    100, nullptr, 2, {}, epoch);
  };
  const auto push_ble = [&] {
    return ble.push_classified(ai_keyboard::kHostActionReportId, report.data(),
                               report.size(), 100,
                               ai_keyboard::HidReportClass::AppCommand, owner).accepted();
  };
  assert(push_usb() && push_ble());
  assert(usb.size() == 1 && ble.size() == 1);
  ai_keyboard::QueuedHidReport usb_front{}, ble_front{};
  assert(usb.front(&usb_front) && ble.front(&ble_front));
  for (const auto* front : {&usb_front, &ble_front}) {
    assert(front->report_id == 0x11 && front->len == 63);
    assert(front->data == report);
    assert(front->report_class == ai_keyboard::HidReportClass::AppCommand);
  }
  assert(usb_front.usb_epoch == epoch && !usb_front.ble_owner.valid());
  assert(ble_front.ble_owner == owner && ble_front.usb_epoch == 0);
  // Two separate presses must not be coalesced as keyboard state snapshots.
  assert(push_usb() && push_ble());
  assert(usb.size() == 2 && ble.size() == 2);
  constexpr auto usb_capacity = ai_keyboard::kHidReportQueueCapacity - 2;
  constexpr auto ble_capacity = ai_keyboard::kHidReportQueueCapacity -
                                2 * ai_keyboard::kKeyboardStateSourceCount;
  for (std::size_t count = 2; count < usb_capacity; ++count) assert(push_usb());
  for (std::size_t count = 2; count < ble_capacity; ++count) assert(push_ble());
  assert(!push_usb() && usb.size() == usb_capacity);
  assert(!push_ble() && ble.size() == ble_capacity);
  for (auto* queue : {&usb, &ble}) {
    ai_keyboard::QueuedHidReport front{};
    while (queue->front(&front)) {
      assert(front.data == report);
      assert(queue->pop_if_sequence(front.sequence));
    }
    assert(queue->empty());
  }
}

}  // namespace

int main() {
  valid_config_is_accepted_without_rewriting();
  canonical_uuid_validation_is_exact_and_has_no_version_or_nil_gate();
  invalid_config_preserves_previous_state_and_is_not_normalized();
  press_emits_one_host_action_and_release_emits_none();
  encoding_has_exact_fields_no_prefix_and_zero_tail();
  all_main_keys_preserve_distinct_host_actions_and_events();
  all_main_keys_mix_host_actions_with_legacy_actions();
  default_keymap_does_not_embed_host_action_test_identities();
  host_action_payload_survives_both_existing_queue_forms();
}
