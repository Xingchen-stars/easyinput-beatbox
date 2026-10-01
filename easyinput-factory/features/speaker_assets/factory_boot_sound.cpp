#include "speaker_assets/factory_boot_sound.h"

#include <cstddef>
#include <cstdint>

namespace easy_input::speaker_assets {
namespace {

extern const std::uint8_t kFactoryBootSoundStart[]
    asm("_binary_waytoagi_eiad_start");
extern const std::uint8_t kFactoryBootSoundEnd[]
    asm("_binary_waytoagi_eiad_end");
extern const std::uint8_t kBeatboxModePromptStart[]
    asm("_binary_mode_beatbox_eiad_start");
extern const std::uint8_t kBeatboxModePromptEnd[]
    asm("_binary_mode_beatbox_eiad_end");
extern const std::uint8_t kEasyInputModePromptStart[]
    asm("_binary_mode_easyinput_eiad_start");
extern const std::uint8_t kEasyInputModePromptEnd[]
    asm("_binary_mode_easyinput_eiad_end");

}  // namespace

FactoryBootSound factory_boot_sound() {
  return {
      kFactoryBootSoundStart,
      static_cast<std::size_t>(
          kFactoryBootSoundEnd - kFactoryBootSoundStart),
  };
}

FactoryBootSound device_mode_prompt(DeviceModePrompt prompt) {
  if (prompt == DeviceModePrompt::Beatbox) {
    return {
        kBeatboxModePromptStart,
        static_cast<std::size_t>(
            kBeatboxModePromptEnd - kBeatboxModePromptStart),
    };
  }
  return {
      kEasyInputModePromptStart,
      static_cast<std::size_t>(
          kEasyInputModePromptEnd - kEasyInputModePromptStart),
  };
}

}  // namespace easy_input::speaker_assets
