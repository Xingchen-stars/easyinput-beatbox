#include <cassert>
#include <cstdint>
#include <string>

#include "keyboard/ble_status_wire.h"

namespace {

void appends_required_ble_fields_within_reserved_budget() {
  const std::string base =
      "{\"schema\":\"ai_keyboard.config_status.v1\",\"phase\":\"battery\"}";
  const auto wire = ai_keyboard::append_ble_status_wire_json(
      base,
      {
          true,
          true,
          UINT16_MAX,
          UINT16_MAX,
          UINT16_MAX,
      });

  assert(wire.size() <= ai_keyboard::kConfigStatusGattSafeLen);
  assert(wire.find(R"("ble":{"connected":1,"valid":1)") != std::string::npos);
  assert(wire.find(R"("idle")") == std::string::npos);
  assert(wire.find(R"("profile")") == std::string::npos);
  assert(wire.find(R"("valid":1)") != std::string::npos);
  assert(wire.find(R"("itvl":65535)") != std::string::npos);
  assert(wire.find(R"("latency":65535)") != std::string::npos);
  assert(wire.find(R"("timeout":65535)") != std::string::npos);
  assert(wire.size() - base.size() <=
         ai_keyboard::kConfigStatusBatteryBleReserveLen);
}

void maximum_reserved_base_still_accepts_worst_case_fragment() {
  const std::string minimum = "{\"x\":\"\"}";
  const auto base_len = ai_keyboard::kConfigStatusGattSafeLen -
                        ai_keyboard::kConfigStatusBatteryBleReserveLen;
  std::string base = "{\"x\":\"";
  base.append(base_len - minimum.size(), 'x');
  base += "\"}";
  assert(base.size() == base_len);

  const auto wire = ai_keyboard::append_ble_status_wire_json(
      base,
      {
          true,
          true,
          UINT16_MAX,
          UINT16_MAX,
          UINT16_MAX,
      });
  assert(wire.size() > base.size());
  assert(wire.size() <= ai_keyboard::kConfigStatusGattSafeLen);
  assert(wire.find(R"("ble":{)") != std::string::npos);
}

void invalid_or_unbudgeted_payload_is_left_unchanged() {
  const auto invalid =
      ai_keyboard::append_ble_status_wire_json("ready", {});
  assert(invalid == "ready");

  std::string oversized(ai_keyboard::kConfigStatusGattSafeLen, 'x');
  oversized.front() = '{';
  oversized.back() = '}';
  assert(ai_keyboard::append_ble_status_wire_json(oversized, {}) == oversized);
}

}  // namespace

void detailed_overlay_preserves_capability_and_obeys_exact_byte_boundary() {
  using namespace ai_keyboard;
  BleDetailedStatusWireSnapshot detail;
  detail.connected = detail.parameters_valid = detail.update_in_flight = true;
  detail.connection_handle = UINT16_MAX - 1;
  detail.interval = detail.latency = detail.supervision_timeout = UINT16_MAX;
  detail.update_status = INT32_MIN;
  detail.queued_reports = detail.hid_queue_high_watermark = detail.queued_wheel_reports = 32;
  detail.enqueued_reports = detail.transmitted_reports = detail.dropped_reports = UINT32_MAX;
  detail.retryable_reports = detail.enqueued_wheel_reports = UINT32_MAX;
  detail.coalesced_wheel_reports = detail.transmitted_wheel_reports = UINT32_MAX;
  detail.dropped_wheel_reports = UINT32_MAX;
  const std::string core = R"({"capabilities":{"host_action_v1":true},"padding":""})";
  const auto enriched = append_ble_detailed_status_wire_json(core, detail);
  assert(enriched.find(R"("upd":-2147483648)") != std::string::npos);
  assert(enriched.find(R"("hid_enq":4294967295)") != std::string::npos);
  assert(enriched.find(R"("wheel_drop":4294967295)") != std::string::npos);
  const auto fragment_len = enriched.size() - core.size();
  for (const auto extra : {0U, 1U}) {
    auto base = core;
    base.insert(base.size() - 2, kConfigStatusGattSafeLen - enriched.size() + extra, 'x');
    const auto wire = append_ble_detailed_status_wire_json(base, detail);
    assert(wire.size() <= kConfigStatusGattSafeLen);
    assert(wire.find(R"("host_action_v1":true)") != std::string::npos);
    if (extra == 0) {
      assert(wire.size() == 512);
      assert(wire.size() == base.size() + fragment_len);
    } else {
      assert(wire == base);
    }
  }
}

int main() {
  detailed_overlay_preserves_capability_and_obeys_exact_byte_boundary();
  appends_required_ble_fields_within_reserved_budget();
  maximum_reserved_base_still_accepts_worst_case_fragment();
  invalid_or_unbudgeted_payload_is_left_unchanged();
  return 0;
}
