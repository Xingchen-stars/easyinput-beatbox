#pragma once

#include <cstddef>
#include <cstdint>

namespace easy_input::speaker_assets {

struct FactoryBootSound {
  const std::uint8_t* encoded = nullptr;
  std::size_t encoded_bytes = 0U;
};

enum class DeviceModePrompt : std::uint8_t {
  Beatbox,
  EasyInput,
};

// Returns the immutable WaytoAGI EIAD v1 fallback embedded in the app image.
// It is never written into sound_a/sound_b and is used only when the Store
// proves that no sound preference has ever committed.
[[nodiscard]] FactoryBootSound factory_boot_sound();

// Immutable mode names used only by the five-second top-level mode selector.
// They use the same EIAD decoder and audio/power arbitration as the Boot asset.
[[nodiscard]] FactoryBootSound device_mode_prompt(DeviceModePrompt prompt);

}  // namespace easy_input::speaker_assets
