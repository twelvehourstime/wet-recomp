// wet-recomp - in-game Controls menu (see controls_menu.cpp)

#pragma once

#include <rex/ui/imgui_dialog.h>

#include <filesystem>
#include <functional>
#include <set>
#include <string>

namespace wet::ui {

class ControlsMenu final : public rex::ui::ImGuiDialog {
 public:
  // The menu exists only while open (like the SDK overlays): a registered dialog makes the
  // presenter repaint continuously and present from the UI thread. It deletes itself after
  // closing and calls on_closed so the owner can drop its pointer.
  ControlsMenu(rex::ui::ImGuiDrawer* drawer, std::filesystem::path config_path,
               std::function<void()> on_closed);
  ~ControlsMenu() override;

  void RequestClose() { Close(); }

 protected:
  void OnDraw(ImGuiIO& io) override;
  void OnClose() override;

 private:
  enum class Capture { kNone, kKeyReplace, kKeyAdd, kPad };

  void SetVisible(bool visible);
  void DrawKeyboardTab();
  void DrawControllerTab();
  void DrawDisplayTab();
  void PollKeyCapture();
  void PollPadCapture();
  void Save();

  std::filesystem::path config_path_;
  std::function<void()> on_closed_;
  bool visible_ = false;

  Capture capture_ = Capture::kNone;
  std::string capture_cvar_;   // keyboard: keybind cvar being edited
  int capture_pad_index_ = -1; // controller: game input being edited
  bool capture_armed_ = false; // all keys/buttons released since capture started
  int lone_modifier_ = 0;      // Shift/Ctrl/Alt held alone during a key capture
  std::string status_;
};

}  // namespace wet::ui
