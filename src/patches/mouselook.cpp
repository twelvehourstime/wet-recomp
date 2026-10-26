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
// The lock-on / slow-motion aim code reads the right stick through its own calls instead of
// the snapshot, so the same per-frame value is mirrored into those readers' vectors
// (config/hooks.toml). Without that the game believes the stick is idle and keeps the
// auto-lock camera, which showed up as "mouse dead while the red marker is on an enemy".
//
// Writing the camera angles directly does not work: with a zero stick the camera's auto-follow
// re-derives the angles every frame and discards the change.

#include <rex/chrono/clock.h>
#include <rex/cvar.h>
#include <rex/input/mnk/mnk_input_driver.h>
#include <rex/ppc/context.h>
#include <rex/system/kernel_state.h>

#include <algorithm>
#include <atomic>
#include <cmath>
#include <cstring>

REXCVAR_DEFINE_BOOL(wet_raw_mouse, true, "Wet/Input",
                    "Apply mouse motion to the camera 1:1 (off: SDK mouse-as-right-stick)");
REXCVAR_DEFINE_DOUBLE(wet_mouse_sensitivity, 0.05, "Wet/Input",
                      "Camera degrees per mouse count")
    .range(0.001, 2.0);
REXCVAR_DEFINE_BOOL(wet_mouse_invert_y, false, "Wet/Input", "Invert mouse vertical look");
REXCVAR_DEFINE_BOOL(wet_mouse_aim_assist, false, "Wet/Input",
                    "Keep the game's aim-lock branch in the camera update while using the mouse");
REXCVAR_DEFINE_BOOL(wet_camera_follow, false, "Wet/Input",
                    "Let the camera turn with the character's heading while moving (game "
                    "default). Off keeps the camera where the mouse put it");
REXCVAR_DEFINE_BOOL(wet_aim_assist, false, "Wet/Input",
                    "Game aim assist (sticky reticle / target magnetism). Off clears the "
                    "WeaponArmManager aim-assist flag every frame, overriding the in-game option");

namespace {

constexpr double kStaleSeconds = 0.25;  // snapshot not updated (menu/cutscene): drop motion

// Player fields (sub_829B8078 / sub_829B1960).
constexpr uint32_t kRightStickX = 164;
constexpr uint32_t kRightStickY = 168;
constexpr uint32_t kCameraParams = 500;  // pointer; +80 yaw speed, +92 pitch speed (rad/s)
constexpr uint32_t kYawSpeed = 80;
constexpr uint32_t kPitchSpeed = 92;

// Stick value computed by the snapshot hook this frame, re-applied by the mirror hooks.
std::atomic<float> g_add_x{0.0f};
std::atomic<float> g_add_y{0.0f};
// Host tick of the last non-zero mouse motion; the mouse counts as "in use" for a while after.
std::atomic<uint64_t> g_last_mouse_tick{0};
constexpr double kMouseInUseSeconds = 1.0;

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

void AddToVec2(uint32_t addr) {
  float ax = g_add_x.load(std::memory_order_relaxed);
  float ay = g_add_y.load(std::memory_order_relaxed);
  if (ax == 0.0f && ay == 0.0f) return;
  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  StoreF32(base, addr, LoadF32(base, addr) + ax);
  StoreF32(base, addr + 4, LoadF32(base, addr + 4) + ay);
}

}  // namespace

void WetMouseLookHook(PPCRegister& r29) {
  float dx = 0, dy = 0;
  double dt = 0;
  rex::input::mnk::TakeRawMouseDelta(dx, dy, dt);
  g_add_x.store(0.0f, std::memory_order_relaxed);
  g_add_y.store(0.0f, std::memory_order_relaxed);
  if (!REXCVAR_GET(wet_raw_mouse) || dt > kStaleSeconds || (dx == 0 && dy == 0)) return;
  g_last_mouse_tick.store(rex::chrono::Clock::QueryHostTickCount(), std::memory_order_relaxed);

  uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
  const uint32_t player = r29.u32;
  const uint32_t params = LoadU32(base, player + kCameraParams);
  if (!params) return;
  const float yaw_speed = std::fabs(LoadF32(base, params + kYawSpeed));
  const float pitch_speed = std::fabs(LoadF32(base, params + kPitchSpeed));
  if (yaw_speed < 1e-4f || pitch_speed < 1e-4f) return;

  dt = std::clamp(dt, 1.0 / 500.0, kStaleSeconds);
  const double rad_per_count = REXCVAR_GET(wet_mouse_sensitivity) * 3.14159265358979 / 180.0;
  // After sub_829A64B8 both snapshot axes have the opposite sign to the raw stick, so mouse
  // right needs a negative X and mouse up (negative dy) a negative Y (both verified in game).
  const double up = REXCVAR_GET(wet_mouse_invert_y) ? -double(dy) : double(dy);
  const float add_x = float(-dx * rad_per_count / (dt * yaw_speed));
  const float add_y = float(up * rad_per_count / (dt * pitch_speed));

  g_add_x.store(add_x, std::memory_order_relaxed);
  g_add_y.store(add_y, std::memory_order_relaxed);
  StoreF32(base, player + kRightStickX, LoadF32(base, player + kRightStickX) + add_x);
  StoreF32(base, player + kRightStickY, LoadF32(base, player + kRightStickY) + add_y);
}

void WetAimAssistHook(PPCRegister& r7) {
  if (REXCVAR_GET(wet_mouse_aim_assist) || !REXCVAR_GET(wet_raw_mouse)) return;
  uint64_t last = g_last_mouse_tick.load(std::memory_order_relaxed);
  if (!last) return;
  double idle = double(rex::chrono::Clock::QueryHostTickCount() - last) /
                double(rex::chrono::Clock::QueryHostTickFrequency());
  if (idle < kMouseInUseSeconds) r7.u64 = 0;  // flag clear: skip the aim lock
}

void WetMouseLookMirror96Hook(PPCRegister& r1) { AddToVec2(r1.u32 + 96); }
void WetMouseLookMirror88Hook(PPCRegister& r1) { AddToVec2(r1.u32 + 88); }

void WetMouseLookMirror80Hook(PPCRegister& r1, PPCRegister& r28) {
  AddToVec2(r1.u32 + 80);
  if (!REXCVAR_GET(wet_aim_assist) && r28.u32) {
    // WeaponArmManager+316 bit 0x800000: aim assist enabled (IsAimAssistEnabled).
    uint8_t* base = rex::system::kernel_state()->memory()->virtual_membase();
    const uint32_t flags_addr = r28.u32 + 316;
    uint32_t flags = LoadU32(base, flags_addr);
    if (flags & 0x800000u) {
      flags &= ~0x800000u;
      uint32_t be = _byteswap_ulong(flags);
      std::memcpy(base + flags_addr, &be, 4);
    }
  }
}

void WetCameraFollowHook(PPCRegister& f12) {
  if (REXCVAR_GET(wet_camera_follow) || !REXCVAR_GET(wet_raw_mouse)) return;
  uint64_t last = g_last_mouse_tick.load(std::memory_order_relaxed);
  if (!last) return;  // mouse never used this session: leave the game's behaviour alone
  f12.f64 = -1.0;     // "timer below threshold": world basis
}
