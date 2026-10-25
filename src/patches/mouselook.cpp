// wet-recomp - raw 1:1 mouse look
//
// The SDK turns mouse motion into right-stick deflection per input poll: small motions vanish
// in the game's dead zone and the response is joystick-like. Instead, mnk_mouse_to_stick is off
// and raw mouse counts are injected into the player's right-stick snapshot after the game's own
// stick processing, as the exact value that turns the camera by the mouse angle:
//   camera:   angle += dt * speed * stick           (sub_829AB470)
//   so:       stick += mouse_radians / (dt * speed)
// Controllers keep working (their stick value is kept and the mouse is added on top).
//
// Writing the angles directly does not work: with a zero stick the camera's auto-follow
// re-derives the angles every frame and discards the change.

#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include <algorithm>
#include <cmath>
#include <cstring>

REXCVAR_DEFINE_BOOL(wet_raw_mouse, true, "Wet/Input",
                    "Apply mouse motion to the camera 1:1 (off: SDK mouse-as-right-stick)");
REXCVAR_DEFINE_DOUBLE(wet_mouse_sensitivity, 0.05, "Wet/Input",
                      "Camera degrees per mouse count")
    .range(0.001, 2.0);
REXCVAR_DEFINE_BOOL(wet_mouse_invert_y, false, "Wet/Input", "Invert mouse vertical look");

namespace {

constexpr double kStaleSeconds = 0.25;  // snapshot not updated (menu/cutscene): drop motion

// Player fields (sub_829B8078 / sub_829B1960).
constexpr uint32_t kRightStickX = 164;
constexpr uint32_t kRightStickY = 168;
constexpr uint32_t kCameraParams = 500;  // pointer; +80 yaw speed, +92 pitch speed (rad/s)
constexpr uint32_t kYawSpeed = 80;
constexpr uint32_t kPitchSpeed = 92;

uint32_t LoadU32(uint8_t* base, uint32_t addr) {
  uint32_t v;
  std::memcpy(&v, base + addr, 4);
  return _byteswap_ulong(v);
}

float LoadF32(uint8_t* base, uint32_t addr) {
  uint32_t v = LoadU32(base, addr);
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

void WetMouseLookHook(PPCRegister& r29) {
  float dx = 0, dy = 0;
  double dt = 0;
  rex::input::mnk::TakeRawMouseDelta(dx, dy, dt);
  if (!REXCVAR_GET(wet_raw_mouse) || dt > kStaleSeconds || (dx == 0 && dy == 0)) return;

  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t player = r29.u32;
  const uint32_t params = LoadU32(base, player + kCameraParams);
  if (!params) return;
  const float yaw_speed = std::fabs(LoadF32(base, params + kYawSpeed));
  const float pitch_speed = std::fabs(LoadF32(base, params + kPitchSpeed));
  if (yaw_speed < 1e-4f || pitch_speed < 1e-4f) return;

  dt = std::clamp(dt, 1.0 / 500.0, kStaleSeconds);
  const double rad_per_count = REXCVAR_GET(wet_mouse_sensitivity) * 3.14159265358979 / 180.0;
  // The snapshot X has the opposite sign to the mouse/raw stick (after sub_829A64B8), so mouse
  // right needs a negative X (verified in game). Mouse up (negative dy) = stick up.
  const double up = REXCVAR_GET(wet_mouse_invert_y) ? double(dy) : -double(dy);
  const float add_x = float(-dx * rad_per_count / (dt * yaw_speed));
  const float add_y = float(up * rad_per_count / (dt * pitch_speed));

  StoreF32(base, player + kRightStickX, LoadF32(base, player + kRightStickX) + add_x);
  StoreF32(base, player + kRightStickY, LoadF32(base, player + kRightStickY) + add_y);
}
