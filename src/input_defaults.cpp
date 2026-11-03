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
  // Default layout: RT Shoot on the left button, X Sword on the right button, LT Wall Run on
  // Shift, B Slide on Control, Y on E, D-pad on the arrow keys. The right stick's camera keys
  // are cleared because they also default to the arrows and the mouse is the camera.
  for (auto [flag, value] : {std::pair{"mnk_mode", "true"},
                             std::pair{"mnk_mouse", "true"},
                             std::pair{"mnk_mouse_to_stick", "false"},
                             std::pair{"keybind_right_trigger", "LMB"},
                             std::pair{"keybind_left_trigger", "Shift"},
                             std::pair{"keybind_b", "Control"},
                             std::pair{"keybind_x", "RMB"},
                             std::pair{"keybind_y", "E"},
                             std::pair{"keybind_dpad_up", "Up"},
                             std::pair{"keybind_dpad_down", "Down"},
                             std::pair{"keybind_dpad_left", "Left"},
                             std::pair{"keybind_dpad_right", "Right"},
                             std::pair{"keybind_rstick_up", ""},
                             std::pair{"keybind_rstick_down", ""},
                             std::pair{"keybind_rstick_left", ""},
                             std::pair{"keybind_rstick_right", ""}}) {
    if (rex::cvar::GetFlagSource(flag) == rex::cvar::Source::kDefault) {
      rex::cvar::SetFlagByName(flag, value);
    }
  }
}

}  // namespace wet
