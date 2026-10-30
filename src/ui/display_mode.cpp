// wet-recomp - display mode (see display_mode.h)
//
// Goes through the SDK's Window object: the SDK runtime is a DLL with its own SDL3, so SDL
// calls made from the exe would act on a second, windowless SDL instance.

#include "ui/display_mode.h"

#include <rex/cvar.h>
#include <rex/logging.h>
#include <rex/ui/window.h>

#include <string>

REXCVAR_DEFINE_STRING(wet_display_mode, "borderless", "Wet/Display",
                      "Display mode: fullscreen (exclusive, desktop resolution), borderless "
                      "(fullscreen window) or windowed");

namespace wet::display {

namespace {
rex::ui::Window* g_window = nullptr;
}  // namespace

void SetWindow(rex::ui::Window* window) { g_window = window; }

Mode Current() {
  const std::string& v = REXCVAR_GET(wet_display_mode);
  if (v == "fullscreen") return Mode::kFullscreen;
  if (v == "windowed") return Mode::kWindowed;
  return Mode::kBorderless;
}

const char* Name(Mode mode) {
  switch (mode) {
    case Mode::kFullscreen: return "fullscreen";
    case Mode::kWindowed: return "windowed";
    default: return "borderless";
  }
}

const char* Label(Mode mode) {
  switch (mode) {
    case Mode::kFullscreen: return "Fullscreen (exclusive)";
    case Mode::kWindowed: return "Windowed";
    default: return "Borderless fullscreen";
  }
}

void ApplyStartupFlag() {
  const Mode mode = Current();
  rex::cvar::SetFlagByName("fullscreen", mode == Mode::kWindowed ? "false" : "true");
  REXLOG_INFO("display: startup mode {}", Name(mode));
}

void ApplyAfterWindowOpen() {
  if (Current() == Mode::kFullscreen) Apply(Mode::kFullscreen);
}

const char* Apply(Mode mode) {
  rex::cvar::SetFlagByName("wet_display_mode", Name(mode));
  if (!g_window) return "No window yet";

  const char* status = "Applied (Save to keep)";
  // Exclusive mode (SDK patch 9) is selected first; it takes effect with the fullscreen flag.
  if (!g_window->SetExclusiveFullscreenMode(mode == Mode::kFullscreen) &&
      mode == Mode::kFullscreen) {
    status = "No exclusive mode available; using borderless";
  }
  // The SDK's change callback calls Window::SetFullscreen, which applies it to the window.
  rex::cvar::SetFlagByName("fullscreen", mode == Mode::kWindowed ? "false" : "true");
  REXLOG_INFO("display: mode {} -> {} (window fullscreen {})", Name(mode), status,
              g_window->IsFullscreen());
  return status;
}

}  // namespace wet::display
