// wet-recomp - display mode (exclusive fullscreen / borderless / windowed)
//
// The SDK's `fullscreen` cvar is borderless desktop fullscreen only. wet_display_mode is the
// setting the user sees; it drives the SDK flag at startup and applies the SDL window state
// (exclusive mode = the desktop resolution's display mode) at runtime.

#pragma once

namespace rex::ui { class Window; }

namespace wet::display {

enum class Mode { kBorderless, kFullscreen, kWindowed };

Mode Current();
const char* Name(Mode mode);
const char* Label(Mode mode);  // menu text

// The SDK window, once it exists (OnCreateDialogs).
void SetWindow(rex::ui::Window* window);
// OnPreSetup: derive the SDK `fullscreen` flag before the window is created.
void ApplyStartupFlag();
// After the window exists (OnCreateDialogs): apply the exclusive mode if selected.
void ApplyAfterWindowOpen();
// Runtime: store the cvar and apply it to the window. Returns a status line for the menu.
const char* Apply(Mode mode);

}  // namespace wet::display
