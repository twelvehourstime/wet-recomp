// wet-recomp - raw 1:1 mouse look
//
// The SDK turns mouse motion into right-stick deflection, which the game then integrates as
// angular velocity (angle += dt * speed * stick): small motions vanish in the dead zone and
// the response is joystick-like. Instead, mnk_mouse_to_stick is turned off and the raw counts
// are added straight onto the camera angles inside the camera update, after the game's own
// stick integration (so controllers still work) and before its pitch clamp.

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include <cmath>
#include <cstring>

REXCVAR_DEFINE_BOOL(wet_raw_mouse, true, "Wet/Input",
                    "Apply mouse motion to the camera 1:1 (off: SDK mouse-as-right-stick)");
REXCVAR_DEFINE_DOUBLE(wet_mouse_sensitivity, 0.05, "Wet/Input",
                      "Camera degrees per mouse count")
    .range(0.001, 2.0);
REXCVAR_DEFINE_BOOL(wet_mouse_invert_y, false, "Wet/Input", "Invert mouse vertical look");

namespace {

constexpr double kStaleSeconds = 0.25;  // camera not updated (menu/cutscene): drop motion

float LoadF32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  v = _byteswap_ulong(v);
  float f;
  std::memcpy(&f, &v, 4);
  return f;
}

void StoreF32(uint8_t* base, uint32_t addr, float f) {
  uint32_t v;
  std::memcpy(&v, &f, 4);
  v = _byteswap_ulong(v);
  std::memcpy(base + addr, &v, 4);
}

}  // namespace

void WetMouseLookHook(PPCRegister& r1, PPCRegister& f30, PPCRegister& f27) {
  float dx = 0, dy = 0;
  double since = 0;
  rex::input::mnk::TakeRawMouseDelta(dx, dy, since);
  if (!REXCVAR_GET(wet_raw_mouse) || since > kStaleSeconds || (dx == 0 && dy == 0)) return;

  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const float rad_per_count = float(REXCVAR_GET(wet_mouse_sensitivity) * 3.14159265358979 / 180.0);
  // Same direction a right-stick deflection would turn: mouse right = stick right,
  // mouse up (negative dy) = stick up.
  const float yaw_sign = f30.f64 < 0 ? -1.0f : 1.0f;
  const float pitch_sign = (f27.f64 < 0 ? -1.0f : 1.0f) * (REXCVAR_GET(wet_mouse_invert_y) ? -1.0f : 1.0f);

  const uint32_t yaw_addr = r1.u32 + 84, pitch_addr = r1.u32 + 80;
  StoreF32(base, yaw_addr, LoadF32(base, yaw_addr) + dx * rad_per_count * yaw_sign);
  StoreF32(base, pitch_addr, LoadF32(base, pitch_addr) - dy * rad_per_count * pitch_sign);
}
