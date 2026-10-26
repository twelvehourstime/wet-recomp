// wet-recomp - offline test of cvar save/load through the SDK (never loads the game)
//
// Usage: cvar_selftest <path.toml>
//   1. sets a few bindings at runtime, saves to <path>, prints the file
//   2. resets them to defaults, loads <path> back, prints the values again

#include <rex/cvar.h>

#include <cstdio>
#include <fstream>
#include <iterator>
#include <string>

int main(int argc, char** argv) {
  if (argc < 2) {
    std::printf("usage: cvar_selftest <path.toml>\n");
    return 1;
  }
  const char* names[] = {"keybind_a", "keybind_right_trigger", "mnk_mode", "wet_mouse_sensitivity"};
  for (const char* n : names) {
    std::printf("before: %-22s = '%s' (source %d)\n", n, rex::cvar::GetFlagByName(n).c_str(),
                int(rex::cvar::GetFlagSource(n)));
  }
  std::printf("set keybind_a=LMB -> %d\n", rex::cvar::SetFlagByName("keybind_a", "LMB"));
  std::printf("set keybind_right_trigger=RMB -> %d\n",
              rex::cvar::SetFlagByName("keybind_right_trigger", "RMB"));
  std::printf("set mnk_mode=true -> %d\n", rex::cvar::SetFlagByName("mnk_mode", "true"));
  rex::cvar::SaveConfig(argv[1]);
  {
    std::ifstream in(argv[1]);
    std::string content((std::istreambuf_iterator<char>(in)), std::istreambuf_iterator<char>());
    std::printf("--- saved file:\n%s--- end\n", content.c_str());
  }

  rex::cvar::ResetAllToDefaults();
  std::printf("after reset: keybind_a='%s'\n", rex::cvar::GetFlagByName("keybind_a").c_str());
  rex::cvar::LoadConfig(argv[1]);
  for (const char* n : names) {
    std::printf("after load: %-22s = '%s' (source %d)\n", n, rex::cvar::GetFlagByName(n).c_str(),
                int(rex::cvar::GetFlagSource(n)));
  }
  return 0;
}
