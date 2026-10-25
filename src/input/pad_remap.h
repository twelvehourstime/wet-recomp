// wet-recomp - controller button remapping (see pad_remap.cpp)

#pragma once

#include <cstdint>
#include <string>
#include <string_view>

namespace wet::pad {

// A game input / physical control: the 14 pad buttons plus both triggers.
struct Control {
  const char* id;     // name stored in config ("A", "LT", "DpadUp", ...)
  const char* label;  // shown in the menu
};

inline constexpr int kControlCount = 16;
const Control& GetControl(int index);
int FindControl(std::string_view id);  // -1 if unknown ("None")

// Physical control currently feeding game input `index`, or "None".
std::string GetSource(int index);
void SetSource(int index, std::string_view source_id);
void ResetToDefaults();

// Install the SDK pad filter that applies the mapping.
void Install();

// Raw (pre-remap) state of the controller, for "press a button" capture:
// bit i set = control i held. 0 when no controller is connected.
uint32_t RawHeldMask();

}  // namespace wet::pad
