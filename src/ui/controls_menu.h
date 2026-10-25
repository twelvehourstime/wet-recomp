// wet-recomp - in-game Controls menu (see controls_menu.cpp)

#pragma once

#include <rex/ui/imgui_dialog.h>

#include <filesystem>
#include <string>

namespace wet::ui {

class ControlsMenu final : public rex::ui::ImGuiDialog {
 public:
  ControlsMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path);
  ~ControlsMenu() override;

  void Toggle();
  bool visible() const { return visible_; }

 protected:
  void OnDraw(ImGuiIO& io) override;

 private:
  enum class Capture { kNone, kKeyReplace, kKeyAdd, kPad };

  void SetVisible(bool visible);
  void DrawKeyboardTab();
  void DrawControllerTab();
  void PollKeyCapture();
  void PollPadCapture();
  void Save();

  std::filesystem::path config_path_;
  bool visible_ = false;
  bool mnk_was_enabled_ = false;

  Capture capture_ = Capture::kNone;
  std::string capture_cvar_;   // keyboard: keybind cvar being edited
  int capture_pad_index_ = -1; // controller: game input being edited
  bool capture_armed_ = false; // all keys/buttons released since capture started
  std::string status_;
};

}  // namespace wet::ui
