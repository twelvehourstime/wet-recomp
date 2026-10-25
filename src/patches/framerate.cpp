// wet-recomp - frame rate unlock
//
// Wet presents with D3D PresentationInterval 2: every swap waits two 60 Hz vblanks
// (30 fps). The D3D present path converts the interval to a vblank count in r11 at
// 0x83066E70; with wet_unlock_60fps that count is lowered from 2 to 1.

#include <rex/cvar.h>
#include <rex/ppc/context.h>

REXCVAR_DEFINE_BOOL(wet_unlock_60fps, true, "Wet",
                    "Present every vblank (60 fps) instead of every second vblank (30 fps)");

void WetPresentIntervalHook(PPCRegister& r11) {
  if (REXCVAR_GET(wet_unlock_60fps) && r11.u32 == 2) r11.u64 = 1;
}
