// wet - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>

#include "crash/crash_handler.h"
#include "debug/memtool.h"
#include "input/pad_remap.h"
#include "input_defaults.h"
#include "ui/controls_menu.h"
#include "ui/display_mode.h"

#include <rex/ui/keybinds.h>

REXCVAR_DECLARE(std::string, perf_log_csv);

class WetApp : public rex::ReXApp {
 public:
  using rex::ReXApp::ReXApp;

  static std::unique_ptr<rex::ui::WindowedApp> Create(
      rex::ui::WindowedAppContext& ctx) {
    // Before the runtime exists, so crashes during setup are reported too.
    wet::crash::Install();
    return std::unique_ptr<WetApp>(new WetApp(ctx, "wet",
        PPCImageConfig));
  }

  // Wet always needs the Xenos GPU emulation; the SDK default (empty) runs headless.
  void OnPreSetup(rex::RuntimeConfig& config) override {
    if (config.gpu_plugin.empty()) config.gpu_plugin = "xenos";
    wet::display::ApplyStartupFlag();  // config is loaded by now; window not created yet
  }

  // SDK v0.10.0 defines --perf_log_csv but never applies it; open the per-frame CSV here.
  void OnPostInitLogging() override {
    if (!REXCVAR_GET(perf_log_csv).empty()) rex::perf::SetCsvLogPath(REXCVAR_GET(perf_log_csv));
    wet::ApplyInputDefaults();
    wet::pad::Install();
    for (const char* n : {"keybind_a", "keybind_right_trigger", "wet_pad_A", "wet_action_A"})
      REXLOG_INFO("controls: at startup {} = '{}' (source {})", n, rex::cvar::GetFlagByName(n),
                  int(rex::cvar::GetFlagSource(n)));
  }

  void OnPostSetup() override {
    // GPU plugin cvars exist only once the plugin DLL is loaded (not yet in OnPreSetup).
    // Measured for Wet (NOTES.md, 2026-10-30): one CPU write invalidating a 4 KB page instead
    // of a 256 KB block uploads ~22% less per frame with no visible game-thread cost.
    if (rex::cvar::GetFlagSource("shared_memory_invalidate_block_log2") ==
        rex::cvar::Source::kDefault) {
      if (!rex::cvar::SetFlagByName("shared_memory_invalidate_block_log2", "12"))
        REXLOG_WARN("wet: could not set shared_memory_invalidate_block_log2");
    }
    wet::memtool::StartIfEnabled();
  }

  void OnConfigurePaths(rex::PathConfig& paths) override { config_path_ = paths.config_path; }

  // Controls menu (keyboard/mouse and controller bindings), toggled with F1.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    wet::display::SetWindow(window());
    wet::display::ApplyAfterWindowOpen();
    controls_drawer_ = drawer;
    rex::ui::RegisterBind("bind_wet_controls", "F1", "Toggle Wet controls menu", [this] {
      if (controls_menu_) {
        controls_menu_->RequestClose();
      } else {
        // Owned by the drawer; it deletes itself after closing and clears the pointer.
        controls_menu_ = new wet::ui::ControlsMenu(controls_drawer_, config_path_,
                                                   [this] { controls_menu_ = nullptr; });
      }
    });
  }

  // Override virtual hooks for customization:
  // void OnLoadXexImage(std::string& xex_image) override {}
  // void OnPostLoadXexImage() override {}
  // std::unique_ptr<rex::ui::ImGuiDialog> CreateAchievementsOverlay() override;
  // std::unique_ptr<rex::ui::AchievementNotificationDialog>
  // CreateAchievementNotificationDialog() override;
  // void OnShutdown() override {}

 private:
  std::filesystem::path config_path_;
  rex::ui::ImGuiDrawer* controls_drawer_ = nullptr;
  wet::ui::ControlsMenu* controls_menu_ = nullptr;  // non-owning, see OnCreateDialogs
};
