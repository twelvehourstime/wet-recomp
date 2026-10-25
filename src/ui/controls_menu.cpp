// wet-recomp - in-game Controls menu (F1)
//
// Keyboard & Mouse tab: the SDK's keybind_* cvars (keyboard/mouse -> virtual controller), with
// "press a key or mouse button" capture, plus mouse-look settings.
// Controller tab: wet_pad_* remapping (pad_remap.cpp) with "press a controller button" capture.
// Save writes every non-default cvar to the config file (<exe>/wet.toml) via the SDK.
//
// While the menu is open, mouse look is released (cursor visible, clickable) and keyboard/mouse
// controller emulation is paused so binding a key does not also act in the game.

#include "ui/controls_menu.h"

#include "input/pad_remap.h"

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ui/keybinds.h>
#include <rex/ui/virtual_key.h>

#include <imgui.h>

#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>

#include <cstdio>

namespace wet::ui {
namespace {

struct KeyRow {
  const char* cvar;
  const char* label;
};

constexpr KeyRow kKeyRows[] = {
    {"keybind_lstick_up", "Move forward"},     {"keybind_lstick_down", "Move back"},
    {"keybind_lstick_left", "Move left"},      {"keybind_lstick_right", "Move right"},
    {"keybind_a", "A"},                        {"keybind_b", "B"},
    {"keybind_x", "X"},                        {"keybind_y", "Y"},
    {"keybind_left_trigger", "LT"},            {"keybind_right_trigger", "RT"},
    {"keybind_left_shoulder", "LB"},           {"keybind_right_shoulder", "RB"},
    {"keybind_lstick_press", "Left stick click"}, {"keybind_rstick_press", "Right stick click"},
    {"keybind_start", "Start"},                {"keybind_back", "Back"},
    {"keybind_dpad_up", "D-pad up"},           {"keybind_dpad_down", "D-pad down"},
    {"keybind_dpad_left", "D-pad left"},       {"keybind_dpad_right", "D-pad right"},
    {"keybind_rstick_up", "Camera up (keys)"}, {"keybind_rstick_down", "Camera down (keys)"},
    {"keybind_rstick_left", "Camera left (keys)"}, {"keybind_rstick_right", "Camera right (keys)"},
};

constexpr const char* kMouseCvars[] = {"wet_mouse_sensitivity", "wet_mouse_invert_y",
                                       "wet_raw_mouse"};

bool IsModifierVk(int vk) {
  return vk == VK_SHIFT || vk == VK_CONTROL || vk == VK_MENU || (vk >= VK_LSHIFT && vk <= VK_RMENU) ||
         vk == VK_LWIN || vk == VK_RWIN;
}

bool Down(int vk) { return (GetAsyncKeyState(vk) & 0x8000) != 0; }

// First held non-modifier key/mouse button that has a binding name, or 0.
int FirstHeldKey() {
  for (int vk = 1; vk < 0xFF; ++vk) {
    if (IsModifierVk(vk) || !Down(vk)) continue;
    if (!rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk)).empty()) return vk;
  }
  return 0;
}

bool AnyHeld() {
  for (int vk = 1; vk < 0xFF; ++vk) {
    if (Down(vk)) return true;
  }
  return false;
}

std::string GetString(const char* cvar) { return rex::cvar::GetFlagByName(cvar); }

}  // namespace

ControlsMenu::ControlsMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path)
    : ImGuiDialog(drawer), config_path_(std::move(config_path)) {}

ControlsMenu::~ControlsMenu() { SetVisible(false); }

void ControlsMenu::Toggle() { SetVisible(!visible_); }

void ControlsMenu::SetVisible(bool visible) {
  if (visible == visible_) return;
  visible_ = visible;
  capture_ = Capture::kNone;
  if (visible) {
    mnk_was_enabled_ = GetString("mnk_mode") == "true";
    rex::cvar::SetFlagByName("mnk_mode", "false");
    rex::input::mnk::SetMouseLookActive(false);
  } else {
    rex::cvar::SetFlagByName("mnk_mode", mnk_was_enabled_ ? "true" : "false");
    rex::input::mnk::SetMouseLookActive(true);
  }
}

void ControlsMenu::PollKeyCapture() {
  if (!capture_armed_) {
    capture_armed_ = !AnyHeld();  // wait for the click on "Bind" to be released
    return;
  }
  if (Down(VK_ESCAPE)) {
    capture_ = Capture::kNone;
    status_ = "Cancelled";
    return;
  }
  int vk = FirstHeldKey();
  if (!vk) return;
  std::string name = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
  std::string mods;
  if (Down(VK_CONTROL)) mods += "Ctrl+";
  if (Down(VK_MENU)) mods += "Alt+";
  if (Down(VK_SHIFT)) mods += "Shift+";
  std::string token = mods + name;
  std::string value = token;
  if (capture_ == Capture::kKeyAdd) {
    std::string current = GetString(capture_cvar_.c_str());
    value = current.empty() ? token : current + "," + token;
  }
  rex::cvar::SetFlagByName(capture_cvar_, value);
  status_ = "Bound " + token;
  capture_ = Capture::kNone;
}

void ControlsMenu::PollPadCapture() {
  uint32_t held = wet::pad::RawHeldMask();
  if (!capture_armed_) {
    capture_armed_ = held == 0;
    return;
  }
  if (Down(VK_ESCAPE)) {
    capture_ = Capture::kNone;
    status_ = "Cancelled";
    return;
  }
  for (int i = 0; i < wet::pad::kControlCount; ++i) {
    if (held & (1u << i)) {
      wet::pad::SetSource(capture_pad_index_, wet::pad::GetControl(i).id);
      status_ = std::string("Bound ") + wet::pad::GetControl(i).label;
      capture_ = Capture::kNone;
      return;
    }
  }
}

void ControlsMenu::DrawKeyboardTab() {
  ImGui::TextDisabled("Bind replaces, Add keeps the existing keys. Esc cancels a capture.");
  if (ImGui::BeginTable("##keys", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableSetupColumn("Keys");
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 150.0f);
    for (const auto& row : kKeyRows) {
      ImGui::PushID(row.cvar);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(row.label);
      ImGui::TableNextColumn();
      bool capturing = (capture_ == Capture::kKeyReplace || capture_ == Capture::kKeyAdd) &&
                       capture_cvar_ == row.cvar;
      if (capturing) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press a key or mouse button...");
      } else {
        std::string v = GetString(row.cvar);
        ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
      }
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Bind")) {
        capture_ = Capture::kKeyReplace;
        capture_cvar_ = row.cvar;
        capture_armed_ = false;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Add")) {
        capture_ = Capture::kKeyAdd;
        capture_cvar_ = row.cvar;
        capture_armed_ = false;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) rex::cvar::SetFlagByName(row.cvar, "");
      ImGui::PopID();
    }
    ImGui::EndTable();
  }

  ImGui::SeparatorText("Mouse look");
  float sens = float(std::atof(GetString("wet_mouse_sensitivity").c_str()));
  if (ImGui::SliderFloat("Sensitivity (deg/count)", &sens, 0.005f, 0.5f, "%.3f",
                         ImGuiSliderFlags_Logarithmic)) {
    char buf[32];
    std::snprintf(buf, sizeof(buf), "%.4f", sens);
    rex::cvar::SetFlagByName("wet_mouse_sensitivity", buf);
  }
  bool invert = GetString("wet_mouse_invert_y") == "true";
  if (ImGui::Checkbox("Invert mouse Y", &invert))
    rex::cvar::SetFlagByName("wet_mouse_invert_y", invert ? "true" : "false");
  bool raw = GetString("wet_raw_mouse") == "true";
  if (ImGui::Checkbox("Raw 1:1 mouse (off = mouse acts as a joystick)", &raw))
    rex::cvar::SetFlagByName("wet_raw_mouse", raw ? "true" : "false");

  if (ImGui::Button("Reset keyboard & mouse to defaults")) {
    for (const auto& row : kKeyRows) rex::cvar::ResetToDefault(row.cvar);
    for (const char* c : kMouseCvars) rex::cvar::ResetToDefault(c);
    status_ = "Keyboard & mouse reset (not saved yet)";
  }
}

void ControlsMenu::DrawControllerTab() {
  ImGui::TextDisabled("Choose which controller button performs each game input. Esc cancels.");
  if (ImGui::BeginTable("##pad", 3, ImGuiTableFlags_RowBg | ImGuiTableFlags_SizingStretchProp)) {
    ImGui::TableSetupColumn("Game input", ImGuiTableColumnFlags_WidthFixed, 170.0f);
    ImGui::TableSetupColumn("Controller button");
    ImGui::TableSetupColumn("##actions", ImGuiTableColumnFlags_WidthFixed, 110.0f);
    for (int i = 0; i < wet::pad::kControlCount; ++i) {
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::TextUnformatted(wet::pad::GetControl(i).label);
      ImGui::TableNextColumn();
      if (capture_ == Capture::kPad && capture_pad_index_ == i) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press a controller button...");
      } else {
        std::string src = wet::pad::GetSource(i);
        int s = wet::pad::FindControl(src);
        ImGui::TextUnformatted(s >= 0 ? wet::pad::GetControl(s).label : "-");
      }
      ImGui::TableNextColumn();
      if (ImGui::SmallButton("Bind")) {
        capture_ = Capture::kPad;
        capture_pad_index_ = i;
        capture_armed_ = false;
      }
      ImGui::SameLine();
      if (ImGui::SmallButton("Clear")) wet::pad::SetSource(i, "None");
      ImGui::PopID();
    }
    ImGui::EndTable();
  }
  bool swap = GetString("wet_pad_swap_sticks") == "true";
  if (ImGui::Checkbox("Swap sticks", &swap))
    rex::cvar::SetFlagByName("wet_pad_swap_sticks", swap ? "true" : "false");
  bool inv = GetString("wet_pad_invert_look_y") == "true";
  if (ImGui::Checkbox("Invert right stick Y", &inv))
    rex::cvar::SetFlagByName("wet_pad_invert_look_y", inv ? "true" : "false");
  if (ImGui::Button("Reset controller to defaults")) {
    wet::pad::ResetToDefaults();
    status_ = "Controller reset (not saved yet)";
  }
}

void ControlsMenu::Save() {
  // Save the real mnk_mode, not the "paused while the menu is open" value.
  rex::cvar::SetFlagByName("mnk_mode", mnk_was_enabled_ ? "true" : "false");
  rex::cvar::SaveConfig(config_path_);
  rex::cvar::SetFlagByName("mnk_mode", "false");
  status_ = "Saved to " + config_path_.filename().string();
}

void ControlsMenu::OnDraw(ImGuiIO& /*io*/) {
  if (!visible_) return;
  if (capture_ == Capture::kKeyReplace || capture_ == Capture::kKeyAdd) PollKeyCapture();
  if (capture_ == Capture::kPad) PollPadCapture();

  ImGui::SetNextWindowSize(ImVec2(640, 620), ImGuiCond_FirstUseEver);
  ImGui::SetNextWindowPos(ImGui::GetMainViewport()->GetCenter(), ImGuiCond_FirstUseEver,
                          ImVec2(0.5f, 0.5f));
  bool open = true;
  if (ImGui::Begin("Controls (F1)##wet", &open, ImGuiWindowFlags_NoCollapse)) {
    if (ImGui::BeginTabBar("##tabs")) {
      if (ImGui::BeginTabItem("Keyboard & Mouse")) {
        DrawKeyboardTab();
        ImGui::EndTabItem();
      }
      if (ImGui::BeginTabItem("Controller")) {
        DrawControllerTab();
        ImGui::EndTabItem();
      }
      ImGui::EndTabBar();
    }
    ImGui::Separator();
    if (ImGui::Button("Save")) Save();
    ImGui::SameLine();
    if (ImGui::Button("Close")) open = false;
    if (!status_.empty()) {
      ImGui::SameLine();
      ImGui::TextDisabled("%s", status_.c_str());
    }
  }
  ImGui::End();
  if (!open) SetVisible(false);
}

}  // namespace wet::ui
