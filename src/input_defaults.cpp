// wet-recomp - input defaults
//
// The SDK ships keyboard/mouse emulation and mouse look switched off. Wet is played with
// mouse look on the right stick, so turn both on unless the user set them (config file,
// environment or command line rank above the compiled default and are left untouched).

#include "input_defaults.h"

#include <rex/cvar.h>

#include <utility>

namespace wet {

void ApplyInputDefaults() {
  // mnk_mouse_to_stick off: Wet applies raw mouse counts to the camera (patches/mouselook.cpp).
  for (auto [flag, value] : {std::pair{"mnk_mode", "true"}, std::pair{"mnk_mouse", "true"},
                             std::pair{"mnk_mouse_to_stick", "false"}}) {
    if (rex::cvar::GetFlagSource(flag) == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName(flag, value);
    }
  }
}

}  // namespace wet
