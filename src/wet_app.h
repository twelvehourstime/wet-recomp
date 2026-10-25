// wet - ReXGlue Recompiled Project
//
// Customize your app by overriding virtual hooks from rex::ReXApp.

#pragma once

#include <rex/cvar.h>
#include <rex/perf/counter.h>
#include <rex/rex_app.h>

#include "crash/crash_handler.h"
#include "debug/memtool.h"
#include "input/pad_remap.h"
#include "input_defaults.h"
#include "ui/controls_menu.h"

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
  }

  // SDK v0.10.0 defines --perf_log_csv but never applies it; open the per-frame CSV here.
  void OnPostInitLogging() override {
    if (!REXCVAR_GET(perf_log_csv).empty()) rex::perf::SetCsvLogPath(REXCVAR_GET(perf_log_csv));
    wet::ApplyInputDefaults();
    wet::pad::Install();
  }

  void OnPostSetup() override { wet::memtool::StartIfEnabled(); }

  void OnConfigurePaths(rex::PathConfig& paths) override { config_path_ = paths.config_path; }

  // Controls menu (keyboard/mouse and controller bindings), toggled with F1.
  void OnCreateDialogs(rex::ui::ImGuiDrawer* drawer) override {
    controls_menu_ = std::make_unique<wet::ui::ControlsMenu>(drawer, config_path_);
    rex::ui::RegisterBind("bind_wet_controls", "F1", "Toggle Wet controls menu",
                          [this] { controls_menu_->Toggle(); });
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
  std::unique_ptr<wet::ui::ControlsMenu> controls_menu_;
};
