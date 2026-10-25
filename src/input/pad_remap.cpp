// wet-recomp - controller button remapping
//
// Each game input (A, B, ... LT, RT) has a cvar wet_pad_<id> naming the physical control that
// produces it (default: itself; "None" disables it). Applied through the SDK pad filter (patch 4)
// to real controllers only, before they are merged with keyboard/mouse, so keyboard bindings are
// unaffected. Buttons and triggers can be cross-mapped: a button driving a trigger gives a full
// pull, a trigger driving a button counts as pressed past the XInput trigger threshold.

#include "input/pad_remap.h"

#include <rex/cvar.h>
#include <rex/input/input.h>
#include <rex/input/input_system.h>

#include <algorithm>
#include <atomic>
#include <cstring>
#include <utility>

REXCVAR_DEFINE_STRING(wet_pad_A, "A", "Wet/Controller", "Physical control for game A");
REXCVAR_DEFINE_STRING(wet_pad_B, "B", "Wet/Controller", "Physical control for game B");
REXCVAR_DEFINE_STRING(wet_pad_X, "X", "Wet/Controller", "Physical control for game X");
REXCVAR_DEFINE_STRING(wet_pad_Y, "Y", "Wet/Controller", "Physical control for game Y");
REXCVAR_DEFINE_STRING(wet_pad_LB, "LB", "Wet/Controller", "Physical control for game LB");
REXCVAR_DEFINE_STRING(wet_pad_RB, "RB", "Wet/Controller", "Physical control for game RB");
REXCVAR_DEFINE_STRING(wet_pad_LT, "LT", "Wet/Controller", "Physical control for game LT");
REXCVAR_DEFINE_STRING(wet_pad_RT, "RT", "Wet/Controller", "Physical control for game RT");
REXCVAR_DEFINE_STRING(wet_pad_Back, "Back", "Wet/Controller", "Physical control for game Back");
REXCVAR_DEFINE_STRING(wet_pad_Start, "Start", "Wet/Controller", "Physical control for game Start");
REXCVAR_DEFINE_STRING(wet_pad_LS, "LS", "Wet/Controller", "Physical control for game left stick click");
REXCVAR_DEFINE_STRING(wet_pad_RS, "RS", "Wet/Controller", "Physical control for game right stick click");
REXCVAR_DEFINE_STRING(wet_pad_DpadUp, "DpadUp", "Wet/Controller", "Physical control for game D-pad up");
REXCVAR_DEFINE_STRING(wet_pad_DpadDown, "DpadDown", "Wet/Controller", "Physical control for game D-pad down");
REXCVAR_DEFINE_STRING(wet_pad_DpadLeft, "DpadLeft", "Wet/Controller", "Physical control for game D-pad left");
REXCVAR_DEFINE_STRING(wet_pad_DpadRight, "DpadRight", "Wet/Controller", "Physical control for game D-pad right");
REXCVAR_DEFINE_BOOL(wet_pad_swap_sticks, false, "Wet/Controller", "Swap left and right sticks");
REXCVAR_DEFINE_BOOL(wet_pad_invert_look_y, false, "Wet/Controller", "Invert right stick vertical");

namespace wet::pad {
namespace {

using namespace rex::input;  // X_INPUT_* types and button bits

constexpr uint8_t kTriggerThreshold = 30;  // XINPUT_GAMEPAD_TRIGGER_THRESHOLD
constexpr int kLT = 6, kRT = 7;

constexpr Control kControls[kControlCount] = {
    {"A", "A"},           {"B", "B"},
    {"X", "X"},           {"Y", "Y"},
    {"LB", "LB"},         {"RB", "RB"},
    {"LT", "LT"},         {"RT", "RT"},
    {"Back", "Back"},     {"Start", "Start"},
    {"LS", "Left stick click"}, {"RS", "Right stick click"},
    {"DpadUp", "D-pad up"},     {"DpadDown", "D-pad down"},
    {"DpadLeft", "D-pad left"}, {"DpadRight", "D-pad right"},
};

// Button bit per control; 0 for the triggers.
constexpr uint16_t kBits[kControlCount] = {
    X_INPUT_GAMEPAD_A,           X_INPUT_GAMEPAD_B,
    X_INPUT_GAMEPAD_X,           X_INPUT_GAMEPAD_Y,
    X_INPUT_GAMEPAD_LEFT_SHOULDER, X_INPUT_GAMEPAD_RIGHT_SHOULDER,
    0,                           0,
    X_INPUT_GAMEPAD_BACK,        X_INPUT_GAMEPAD_START,
    X_INPUT_GAMEPAD_LEFT_THUMB,  X_INPUT_GAMEPAD_RIGHT_THUMB,
    X_INPUT_GAMEPAD_DPAD_UP,     X_INPUT_GAMEPAD_DPAD_DOWN,
    X_INPUT_GAMEPAD_DPAD_LEFT,   X_INPUT_GAMEPAD_DPAD_RIGHT,
};

std::string* const kSourceCvars[kControlCount] = {
    &REXCVAR_GET(wet_pad_A),      &REXCVAR_GET(wet_pad_B),      &REXCVAR_GET(wet_pad_X),
    &REXCVAR_GET(wet_pad_Y),      &REXCVAR_GET(wet_pad_LB),     &REXCVAR_GET(wet_pad_RB),
    &REXCVAR_GET(wet_pad_LT),     &REXCVAR_GET(wet_pad_RT),     &REXCVAR_GET(wet_pad_Back),
    &REXCVAR_GET(wet_pad_Start),  &REXCVAR_GET(wet_pad_LS),     &REXCVAR_GET(wet_pad_RS),
    &REXCVAR_GET(wet_pad_DpadUp), &REXCVAR_GET(wet_pad_DpadDown), &REXCVAR_GET(wet_pad_DpadLeft),
    &REXCVAR_GET(wet_pad_DpadRight),
};

std::atomic<uint32_t> g_raw_held{0};

// Parsed mapping read by the input thread; the cvar strings are only touched on change.
std::atomic<int> g_source[kControlCount];

void RefreshMapping() {
  for (int i = 0; i < kControlCount; ++i) {
    g_source[i].store(FindControl(*kSourceCvars[i]), std::memory_order_relaxed);
  }
}

// Analog value 0..255 of physical control i in a raw state.
uint8_t Value(const X_INPUT_GAMEPAD& g, int i) {
  if (i == kLT) return g.left_trigger;
  if (i == kRT) return g.right_trigger;
  return (uint16_t(g.buttons) & kBits[i]) ? 255 : 0;
}

void Apply(X_INPUT_STATE& state) {
  const X_INPUT_GAMEPAD raw = state.gamepad;

  uint32_t held = 0;
  for (int i = 0; i < kControlCount; ++i) {
    if (Value(raw, i) >= kTriggerThreshold) held |= 1u << i;
  }
  g_raw_held.store(held, std::memory_order_relaxed);

  uint16_t buttons = uint16_t(raw.buttons) & X_INPUT_GAMEPAD_GUIDE;  // guide is not remappable
  uint8_t lt = 0, rt = 0;
  for (int target = 0; target < kControlCount; ++target) {
    int source = g_source[target].load(std::memory_order_relaxed);
    if (source < 0) continue;
    uint8_t v = Value(raw, source);
    if (target == kLT) {
      lt = std::max(lt, v);
    } else if (target == kRT) {
      rt = std::max(rt, v);
    } else if (v >= kTriggerThreshold) {
      buttons |= kBits[target];
    }
  }
  state.gamepad.buttons = buttons;
  state.gamepad.left_trigger = lt;
  state.gamepad.right_trigger = rt;

  if (REXCVAR_GET(wet_pad_swap_sticks)) {
    state.gamepad.thumb_lx = raw.thumb_rx;
    state.gamepad.thumb_ly = raw.thumb_ry;
    state.gamepad.thumb_rx = raw.thumb_lx;
    state.gamepad.thumb_ry = raw.thumb_ly;
  }
  if (REXCVAR_GET(wet_pad_invert_look_y)) {
    int16_t ry = state.gamepad.thumb_ry;
    state.gamepad.thumb_ry = ry == INT16_MIN ? INT16_MAX : int16_t(-ry);
  }
}

}  // namespace

const Control& GetControl(int index) { return kControls[index]; }

int FindControl(std::string_view id) {
  for (int i = 0; i < kControlCount; ++i) {
    if (id == kControls[i].id) return i;
  }
  return -1;
}

std::string GetSource(int index) { return *kSourceCvars[index]; }

void SetSource(int index, std::string_view source_id) {
  std::string name = std::string("wet_pad_") + kControls[index].id;
  rex::cvar::SetFlagByName(name, source_id.empty() ? "None" : source_id);
}

void ResetToDefaults() {
  for (int i = 0; i < kControlCount; ++i) SetSource(i, kControls[i].id);
  rex::cvar::SetFlagByName("wet_pad_swap_sticks", "false");
  rex::cvar::SetFlagByName("wet_pad_invert_look_y", "false");
}

void Install() {
  RefreshMapping();
  for (int i = 0; i < kControlCount; ++i) {
    rex::cvar::RegisterChangeCallback(std::string("wet_pad_") + kControls[i].id,
                                      [](std::string_view, std::string_view) { RefreshMapping(); });
  }
  rex::input::SetPadStateFilter(Apply);
}

uint32_t RawHeldMask() { return g_raw_held.load(std::memory_order_relaxed); }

}  // namespace wet::pad
