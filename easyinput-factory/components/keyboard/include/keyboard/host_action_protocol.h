#pragma once

#include <algorithm>
#include <array>
#include <cstddef>
#include <cstdint>
#include <string_view>

#include "keyboard/fixed_text_protocol.h"

namespace ai_keyboard {

inline constexpr std::string_view kHostActionPrefix = "host_action:";
inline constexpr std::size_t kHostActionUuidLen = 36;
inline constexpr std::uint8_t kHostActionReportId = kFixedTextAppCommandReportId;
inline constexpr std::uint8_t kHostActionCommandKind = 0x05;
using HostActionReport = std::array<std::uint8_t, kFixedTextAppCommandPayloadLen>;

static_assert(kHostActionUuidLen <= kFixedTextAppCommandChunkDataLen);

// Format only: no UUID version, variant or nil restrictions, and no normalization.
inline bool is_canonical_lowercase_uuid(std::string_view uuid) {
  if (uuid.size() != kHostActionUuidLen) {
    return false;
  }
  for (std::size_t index = 0; index < uuid.size(); ++index) {
    const char value = uuid[index];
    if (index == 8 || index == 13 || index == 18 || index == 23) {
      if (value != '-') {
        return false;
      }
    } else if (!((value >= '0' && value <= '9') ||
                 (value >= 'a' && value <= 'f'))) {
      return false;
    }
  }
  return true;
}

inline bool is_host_action_config(std::string_view value) {
  return value.size() == kHostActionPrefix.size() + kHostActionUuidLen &&
         value.substr(0, kHostActionPrefix.size()) == kHostActionPrefix &&
         is_canonical_lowercase_uuid(value.substr(kHostActionPrefix.size()));
}

// The config/event keeps its prefix; only this wire encoder removes it.
// Report ID is supplied separately to the transport, not inside the payload.
inline bool encode_host_action_report(std::string_view value,
                                      HostActionReport* out) {
  if (out == nullptr) {
    return false;
  }
  if (!is_host_action_config(value)) {
    out->fill(0);
    return false;
  }
  HostActionReport report{};
  report[0] = kHostActionCommandKind;
  report[1] = 0;
  report[2] = 1;
  report[3] = kHostActionUuidLen;
  const auto uuid = value.substr(kHostActionPrefix.size());
  std::copy(uuid.begin(), uuid.end(), report.begin() + kFixedTextAppCommandHeaderLen);
  *out = report;
  return true;
}

}  // namespace ai_keyboard
