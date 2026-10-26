// wet-recomp - in-game Controls menu (F1)
//
// Keyboard & Mouse tab: the SDK's keybind_* cvars (keyboard/mouse -> virtual controller), with
// "press a key or mouse button" capture, plus mouse-look settings.
// Controller tab: wet_pad_* remapping (pad_remap.cpp) with "press a controller button" capture.
// Save writes every non-default cvar to the config file (<exe>/wet.toml) via the SDK.
//
// While the menu is open, mouse look is released (cursor visible, clickable) and keyboard/mouse
// controller emulation is suppressed (SDK patch) so binding a key does not also act in the game.
// Shift/Ctrl/Alt pressed and released on their own bind as bare keys; held together with
// another key they form a combination ("Shift+Up").
//
// Each game input also has an editable in-game action name (wet_action_<input>, "Wet/Actions"),
// shown next to it in both tabs and saved with the bindings.

#include "ui/controls_menu.h"

#include "input/pad_remap.h"

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/logging.h>
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
#include <fstream>
#include <string_view>
#include <vector>

REXCVAR_DEFINE_STRING(wet_action_A, "Jump", "Wet/Actions", "In-game action of A");
REXCVAR_DEFINE_STRING(wet_action_B, "Slide", "Wet/Actions", "In-game action of B");
REXCVAR_DEFINE_STRING(wet_action_X, "Melee", "Wet/Actions", "In-game action of X");
REXCVAR_DEFINE_STRING(wet_action_Y, "Action", "Wet/Actions", "In-game action of Y");
REXCVAR_DEFINE_STRING(wet_action_LB, "Wall Run", "Wet/Actions", "In-game action of LB");
REXCVAR_DEFINE_STRING(wet_action_RB, "Shoot", "Wet/Actions", "In-game action of RB");
REXCVAR_DEFINE_STRING(wet_action_LT, "", "Wet/Actions", "In-game action of LT");
REXCVAR_DEFINE_STRING(wet_action_RT, "", "Wet/Actions", "In-game action of RT");
REXCVAR_DEFINE_STRING(wet_action_Back, "Move List", "Wet/Actions", "In-game action of Back");
REXCVAR_DEFINE_STRING(wet_action_Start, "Pause", "Wet/Actions", "In-game action of Start");
REXCVAR_DEFINE_STRING(wet_action_LS, "", "Wet/Actions", "In-game action of left stick click");
REXCVAR_DEFINE_STRING(wet_action_RS, "", "Wet/Actions", "In-game action of right stick click");
REXCVAR_DEFINE_STRING(wet_action_DpadUp, "", "Wet/Actions", "In-game action of D-pad up");
REXCVAR_DEFINE_STRING(wet_action_DpadDown, "", "Wet/Actions", "In-game action of D-pad down");
REXCVAR_DEFINE_STRING(wet_action_DpadLeft, "", "Wet/Actions", "In-game action of D-pad left");
REXCVAR_DEFINE_STRING(wet_action_DpadRight, "", "Wet/Actions", "In-game action of D-pad right");

namespace wet::ui {
namespace {

struct KeyRow {
  const char* cvar;
  const char* label;   // controller input this key drives
  const char* pad_id;  // game input with a renamable action; nullptr = fixed action below
  const char* fixed_action;
};

constexpr KeyRow kKeyRows[] = {
    {"keybind_lstick_up", "Left stick", nullptr, "Move forward"},
    {"keybind_lstick_down", "Left stick", nullptr, "Move back"},
    {"keybind_lstick_left", "Left stick", nullptr, "Move left"},
    {"keybind_lstick_right", "Left stick", nullptr, "Move right"},
    {"keybind_right_shoulder", "RB", "RB", nullptr},
    {"keybind_left_shoulder", "LB", "LB", nullptr},
    {"keybind_a", "A", "A", nullptr},
    {"keybind_b", "B", "B", nullptr},
    {"keybind_x", "X", "X", nullptr},
    {"keybind_y", "Y", "Y", nullptr},
    {"keybind_right_trigger", "RT", "RT", nullptr},
    {"keybind_left_trigger", "LT", "LT", nullptr},
    {"keybind_back", "Back", "Back", nullptr},
    {"keybind_start", "Start", "Start", nullptr},
    {"keybind_lstick_press", "Left stick click", "LS", nullptr},
    {"keybind_rstick_press", "Right stick click", "RS", nullptr},
    {"keybind_dpad_up", "D-pad up", "DpadUp", nullptr},
    {"keybind_dpad_down", "D-pad down", "DpadDown", nullptr},
    {"keybind_dpad_left", "D-pad left", "DpadLeft", nullptr},
    {"keybind_dpad_right", "D-pad right", "DpadRight", nullptr},
    {"keybind_rstick_up", "Right stick", nullptr, "Camera up (keys)"},
    {"keybind_rstick_down", "Right stick", nullptr, "Camera down (keys)"},
    {"keybind_rstick_left", "Right stick", nullptr, "Camera left (keys)"},
    {"keybind_rstick_right", "Right stick", nullptr, "Camera right (keys)"},
};

constexpr float kFontScale = 1.5f;

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

// Inline editor for the in-game action name of game input `pad_id`.
void ActionNameField(const char* pad_id) {
  std::string cvar = std::string("wet_action_") + pad_id;
  char buf[64];
  std::snprintf(buf, sizeof(buf), "%s", rex::cvar::GetFlagByName(cvar).c_str());
  ImGui::SetNextItemWidth(-FLT_MIN);
  if (ImGui::InputTextWithHint("##action", "(name it)", buf, sizeof(buf))) {
    rex::cvar::SetFlagByName(cvar, buf);
  }
}

}  // namespace

ControlsMenu::ControlsMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path,
                           std::function<void()> on_closed)
    : ImGuiDialog(drawer), config_path_(std::move(config_path)), on_closed_(std::move(on_closed)) {
  SetVisible(true);
}

ControlsMenu::~ControlsMenu() { SetVisible(false); }

void ControlsMenu::OnClose() {
  SetVisible(false);
  if (on_closed_) on_closed_();
}

void ControlsMenu::SetVisible(bool visible) {
  if (visible == visible_) return;
  visible_ = visible;
  capture_ = Capture::kNone;
  rex::input::mnk::SetMnkInputSuppressed(visible);
  rex::input::mnk::SetMouseLookActive(!visible);
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
  std::string token;
  if (int vk = FirstHeldKey()) {
    std::string mods;
    if (Down(VK_CONTROL)) mods += "Ctrl+";
    if (Down(VK_MENU)) mods += "Alt+";
    if (Down(VK_SHIFT)) mods += "Shift+";
    token = mods + rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(vk));
    lone_modifier_ = 0;
  } else if (Down(VK_SHIFT) || Down(VK_CONTROL) || Down(VK_MENU)) {
    // Remember a modifier held on its own; it binds when released without another key.
    if (!lone_modifier_) lone_modifier_ = Down(VK_SHIFT) ? VK_SHIFT : Down(VK_CONTROL) ? VK_CONTROL : VK_MENU;
    return;
  } else if (lone_modifier_) {
    token = rex::ui::VirtualKeyToString(static_cast<rex::ui::VirtualKey>(lone_modifier_));
    lone_modifier_ = 0;
  } else {
    return;
  }
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
    ImGui::TableSetupColumn("Input", ImGuiTableColumnFlags_WidthFixed, 150.0f * kFontScale);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f * kFontScale);
    ImGui::TableSetupColumn("Keys");
    ImGui::TableHeadersRow();
    for (const auto& row : kKeyRows) {
      ImGui::PushID(row.cvar);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(row.label);
      ImGui::TableNextColumn();
      if (row.pad_id) {
        ActionNameField(row.pad_id);
      } else {
        ImGui::AlignTextToFramePadding();
        ImGui::TextUnformatted(row.fixed_action);
      }
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      bool capturing = (capture_ == Capture::kKeyReplace || capture_ == Capture::kKeyAdd) &&
                       capture_cvar_ == row.cvar;
      if (capturing) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press a key or mouse button...");
      } else {
        std::string v = GetString(row.cvar);
        ImGui::TextUnformatted(v.empty() ? "-" : v.c_str());
      }
      ImGui::SameLine(0.0f, 20.0f);  // buttons directly after the bound keys
      if (ImGui::Button("Bind")) {
        capture_ = Capture::kKeyReplace;
        capture_cvar_ = row.cvar;
        capture_armed_ = false;
        lone_modifier_ = 0;
      }
      ImGui::SameLine();
      if (ImGui::Button("Add")) {
        capture_ = Capture::kKeyAdd;
        capture_cvar_ = row.cvar;
        capture_armed_ = false;
        lone_modifier_ = 0;
      }
      ImGui::SameLine();
      if (ImGui::Button("Clear")) rex::cvar::SetFlagByName(row.cvar, "");
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
    ImGui::TableSetupColumn("Game input", ImGuiTableColumnFlags_WidthFixed, 150.0f * kFontScale);
    ImGui::TableSetupColumn("Action", ImGuiTableColumnFlags_WidthFixed, 200.0f * kFontScale);
    ImGui::TableSetupColumn("Controller button");
    ImGui::TableHeadersRow();
    for (int i = 0; i < wet::pad::kControlCount; ++i) {
      ImGui::PushID(i);
      ImGui::TableNextRow();
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      ImGui::TextUnformatted(wet::pad::GetControl(i).label);
      ImGui::TableNextColumn();
      ActionNameField(wet::pad::GetControl(i).id);
      ImGui::TableNextColumn();
      ImGui::AlignTextToFramePadding();
      if (capture_ == Capture::kPad && capture_pad_index_ == i) {
        ImGui::TextColored(ImVec4(1.0f, 0.8f, 0.2f, 1.0f), "Press a controller button...");
      } else {
        std::string src = wet::pad::GetSource(i);
        int s = wet::pad::FindControl(src);
        ImGui::TextUnformatted(s >= 0 ? wet::pad::GetControl(s).label : "-");
      }
      ImGui::SameLine(0.0f, 20.0f);  // buttons directly after the bound button
      if (ImGui::Button("Bind")) {
        capture_ = Capture::kPad;
        capture_pad_index_ = i;
        capture_armed_ = false;
        lone_modifier_ = 0;
      }
      ImGui::SameLine();
      if (ImGui::Button("Clear")) wet::pad::SetSource(i, "None");
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

// Writes the config file itself rather than through rex::cvar::SaveConfig: that writes every
// non-default cvar, which also captured one-off launch flags (log file, perf CSV, game root)
// and gave no way to see what was written. Only values the user set in-app or that came from
// the file are persisted, and every input binding is written explicitly so a binding that
// happens to equal its default still round-trips.
void ControlsMenu::Save() {
  std::vector<std::string> names;
  for (const auto& row : kKeyRows) names.push_back(row.cvar);
  for (int i = 0; i < wet::pad::kControlCount; ++i) {
    names.push_back(std::string("wet_pad_") + wet::pad::GetControl(i).id);
    names.push_back(std::string("wet_action_") + wet::pad::GetControl(i).id);
  }
  for (const char* c : {"wet_mouse_sensitivity", "wet_mouse_invert_y", "wet_raw_mouse",
                        "wet_pad_swap_sticks", "wet_pad_invert_look_y", "bind_wet_controls"})
    names.push_back(c);
  std::set<std::string> explicit_names(names.begin(), names.end());

  std::ofstream out(config_path_);
  if (!out) {
    status_ = "Could not write " + config_path_.string();
    REXLOG_ERROR("controls: {}", status_);
    return;
  }
  out << "# wet-recomp settings (written by the F1 Controls menu)\n";
  int written = 0;
  auto write = [&](const std::string& name) {
    std::string value = rex::cvar::GetFlagByName(name);
    bool is_number = !value.empty() && (value == "true" || value == "false" ||
                                        value.find_first_not_of("0123456789.-") == std::string::npos);
    out << name << " = " << (is_number ? value : "\"" + value + "\"") << "\n";
    ++written;
  };
  for (const auto& name : names) write(name);
  // Other settings the user changed in-app (settings overlay, console) or loaded from the file.
  for (const auto& name : rex::cvar::ListFlags()) {
    if (explicit_names.count(name)) continue;
    auto src = rex::cvar::GetFlagSource(name);
    if ((src == rex::cvar::Source::kRuntime || src == rex::cvar::Source::kConfig) &&
        rex::cvar::HasNonDefaultValue(name) && name != "mnk_mode" && name != "mnk_mouse" &&
        name != "mnk_mouse_to_stick") {
      write(name);
    }
  }
  status_ = "Saved " + std::to_string(written) + " settings to " + config_path_.filename().string();
  REXLOG_INFO("controls: {} (keybind_a={})", status_, rex::cvar::GetFlagByName("keybind_a"));
}

void ControlsMenu::OnDraw(ImGuiIO& /*io*/) {
  if (capture_ == Capture::kKeyReplace || capture_ == Capture::kKeyAdd) PollKeyCapture();
  if (capture_ == Capture::kPad) PollPadCapture();

  // Large, readable window: ~70% x 85% of the screen, 1.5x text, roomy buttons.
  const ImGuiViewport* vp = ImGui::GetMainViewport();
  ImGui::SetNextWindowSize(ImVec2(vp->WorkSize.x * 0.70f, vp->WorkSize.y * 0.85f),
                           ImGuiCond_Appearing);
  ImGui::SetNextWindowPos(vp->GetCenter(), ImGuiCond_Appearing, ImVec2(0.5f, 0.5f));
  ImGui::PushStyleVar(ImGuiStyleVar_FramePadding, ImVec2(12.0f, 7.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_ItemSpacing, ImVec2(12.0f, 9.0f));
  ImGui::PushStyleVar(ImGuiStyleVar_CellPadding, ImVec2(10.0f, 6.0f));
  bool open = true;
  if (ImGui::Begin("Controls (F1)##wet", &open, ImGuiWindowFlags_NoCollapse)) {
    ImGui::SetWindowFontScale(kFontScale);
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
  ImGui::PopStyleVar(3);
  if (!open) Close();  // deleted by the drawer after this draw; OnClose restores input
}

}  // namespace wet::ui
