# Project notes (working memory)

## Fixed decisions
- Game: Wet (X360), title ID 425307DB. ISO: E:\WET\WET.iso (outside the repo).
- Repo: E:\WET\wet-recomp. Game data and generated code stay out of git (.gitignore).
- SDK: ReXGlue v0.10.0 (stable, 2026-08-21), built from source at that tag. Requires Clang 18+.
- SDK fixes go in patches/rexglue-v0.10.0.diff only.
- Daily-driver build: RelWithDebInfo.

## Step 0: Xenia Canary compatibility (2026-10-25)
- Canary issue xenia-canary/game-compatibility#217: state-playable, Havok middleware.
  apu-slow removed 2026-05. Remaining: shader-compile stutter (async shaders fix it),
  flicker at 1440p (none at 720p). 100% completion reported Dec 2024.
- Old Xenia #671: needed NtQueueApcThread (2020).
- Conclusion: go with ReXGlue. Watch for shader stutter, high-res flicker, NtQueueApcThread.

## Environment (verified 2026-10-25)
- Present: git 2.55, CMake 4.4.4, Ninja 1.13.2, winget 1.29.
- Missing at start: Clang, Python (Store stub only), JDK, Ghidra.

## Log
- 2026-10-25: repo initialized.
- 2026-10-25: toolchain chosen (pending user approval to download):
  LLVM 22.1.8 (matches ReXGlue v0.10.0 Windows CI: choco llvm on 2026-08-21, before 23.1.0),
  Python 3.14.9, Temurin JDK 21.0.12.1+1, Ghidra 12.1.3 (latest Ghidra 12.2 has no XEX loader yet),
  XEXLoaderWV 12.1.3. MSVC BuildTools 14.44 + Windows SDK 10.0.26100 already installed.

## Toolchain (installed 2026-10-25, all SHA-256 verified against published digests)
| Tool | Version | Location |
|---|---|---|
| LLVM/Clang | 22.1.8 | C:\Program Files\LLVM\bin (NOT on PATH; build scripts use full path) |
| Python | 3.14.9 | C:\Program Files\Python314 (all-users; on PATH) |
| JDK | Temurin 21.0.12.1+1 | C:\Program Files\Eclipse Adoptium\jdk-21.0.12.101-hotspot (JAVA_HOME set) |
| Ghidra | 12.1.3 | E:\Tools\ghidra_12.1.3_PUBLIC |
| XEXLoaderWV | 12.1.3 | E:\Tools\ghidra_12.1.3_PUBLIC\Ghidra\Extensions\XEXLoaderWV |
- Installers kept in E:\Tools\_downloads.
- Gotcha: Python per-user install fails (0x80070003, per-user Package Cache) from this
  shell even elevated; InstallAllUsers=1 elevated works.
- Gotcha: shells opened before the install don't see the new PATH/JAVA_HOME; reload from registry.
- Not yet verified: Ghidra actually loads XEXLoaderWV (check on first XEX import).

## ReXGlue SDK build (2026-10-25)
- Source: third_party/rexglue-sdk @ v0.10.0 (f5337cd), git-ignored; `scripts/build.py sdk`
  clones it if missing, applies patches/rexglue-v0.10.0.diff, builds RelWithDebInfo, installs.
- Install prefix: third_party/rexglue-sdk/out/install/win-amd64 (runtime DLLs: rexruntimerd,
  rexgpu-xenosrd, TracyClientrd). Backend: D3D12 (Vulkan OFF on Windows by default).
- CLI is NOT installed; use third_party/rexglue-sdk/out/win-amd64/RelWithDebInfo/rexgluerd.exe
  (`rd` = RelWithDebInfo postfix). Subcommands: init, codegen, recompile-tests.
- The SDK install registers itself in the CMake user package registry
  (HKCU\Software\Kitware\CMake\Packages\rexglue) so find_package(rexglue) works.
- Build: 656 steps, 469 warnings (third-party), 0 errors.
- Patch 1: libmspack symlinks (see patches/README.md). Other symlinks: moltenvk (mac only),
  o1heap/CLAUDE.md (docs) - harmless.
- Gotcha: `cmd; tail log` reports tail's exit code; build logs now end with the real "exit N".

## Game extraction + rexglue init (2026-10-25)
- tools/xdvdfs_extract.py (own, read-only): WET.iso is an XGD2 dump, game partition at
  0xFD90000. Extracted to E:\WET\game: 13,355 files, 4,588,315,622 bytes (matches file table).
- default.xex: XEX2, title ID 425307DB, media ID 666E3BB8, version 3, disc 1/1,
  encrypted + basic-compressed. Image base 0x82000000, stack 0x80000. No DLLs/XEXP on disc.
- `rexglue init --project-name wet --xex-path E:/WET/game/default.xex --game-root E:/WET/game
  --project-root .` wrote CMakeLists.txt, CMakePresets.json, wet_manifest.toml,
  src/main.cpp, src/wet_app.h, generated/rexglue.cmake. Paths were made relative (../game).
- generated/rexglue.cmake is SDK boilerplate (committed); generated/default/ (codegen output)
  stays ignored.
- Game's own CMakePresets are single-config Ninja (win-amd64-relwithdebinfo is our daily driver).

## Codegen (2026-10-25)
- First run: validation failed with 10 UnresolvedCall (8 unique targets): small functions only
  reached by tail calls (`b`), never `bl`, so the analyzer had no entry for them.
  Fixed with config/functions.toml (included from wet_manifest.toml), sizes from
  tools/analysis/tailcall_sizes.py + branch-range check. 0x82F9EB00 is 0x50 (codegen split it).
- Result: exit 0, ~74 s, 475 files, 73,120 functions, 248 MB in generated/default/
  (234 wet_recomp.N.cpp). No unimplemented-instruction markers.
- Imports: 231. All present in SDK by name. Stubs (REX_EXPORT_STUB): IoCheckShareAccess,
  IoCompleteRequest, IoDeleteDevice, IoDismountVolume, IoRemoveShareAccess, IoSetShareAccess,
  ObIsTitleObject, ObReferenceObject, RtlUpcaseUnicodeChar, XamShowAchievementsUI/FriendsUI/
  MarketplaceUI, XamUserCreateStatsEnumerator, XeCryptBnQwBeSigVerify, XeCryptRotSumSha,
  XeKeysGetKey. No REX_EXPORT line found (probably varargs handled elsewhere; verify at link):
  DbgPrint, _snprintf, _snwprintf, _vscwprintf, _vsnprintf, sprintf, swprintf, vswprintf.
  Watch: XeCrypt*/XeKeysGetKey stubs may break save-signature checks; RtlUpcaseUnicodeChar
  stub may break case-insensitive string compares. NtQueueApcThread (old Xenia blocker) is
  implemented.
- Ghidra: XEXLoaderWV 12.1.3 verified working headless (retail decrypt OK). Project and
  memory dumps in E:\WET\work (outside repo). tools/ghidra/DumpAround.java dumps blocks +
  disassembly windows for addresses in E:\WET\work\addrs.txt.
- Sections: .text 0x82510000-0x8338A54F, BINK 0x8338A600 (Bink video), .data 0x833A4600,
  .pdata 0x824A7200. Symbols: symbols/wet.sym.

## Crash reporter (2026-10-25)
- src/crash/crash_handler.{h,cpp}, installed from WetApp::Create (before runtime setup).
  Hooks: SetUnhandledExceptionFilter (last in chain; SDK VEH + guest SEH run first), SIGABRT
  (REX_FATAL calls std::abort), std::terminate, purecall, CRT invalid parameter.
  _set_abort_behavior disables the CRT abort dialog/WER.
- Writes <exe dir>/crashes/crash-<time>-<pid>.txt + .dmp; keeps newest 20 files.
  Report: exception + AV address (with guest address if inside guest arena), host regs,
  DbgHelp-symbolized host stack with file:line, guest PPCContext (r0-31, lr, ctr, msr, xer,
  cr0-7, fpscr, f0-31, last_indirect_target), guest back-chain walk ([sp] = caller sp,
  saved LR at caller_sp-8). Dump: MiniDumpNormal|ThreadInfo|UnloadedModules|
  IndirectlyReferencedMemory, written from a helper thread, report as CommentStreamA.
- WET_CRASH_NO_DIALOG=1 suppresses the message box.
- Verified with crash_selftest.exe (never loads the game): av, abort, terminate, purecall,
  overflow, cxx all produce report + dump; dumps 115 KB (1.1 MB for stack overflow);
  dump stream directory verified (Exception, ThreadList, ModuleList, CommentStreamA, ...).
- NOT yet verified: the guest-register / guest-stack part (needs a crash on a guest thread).

## First game build (2026-10-25)
- SDK installs the rexglue CLI only for Release (cmake/rexglue_install.cmake), so the
  game's find_package failed with "IMPORTED_LOCATION not set for rex::rexglue". build.py sdk
  now installs RelWithDebInfo + Release. No SDK patch needed.
- init template omits GPU_PLUGINS: changed to rexglue_setup_target(wet GPU_PLUGINS xenos)
  so rexgpu-xenosrd.dll is staged next to wet.exe.
- `build.py game --target wet --jobs 20`: 243 steps, 151 s, 0 warnings, 0 errors.
  wet.exe 84 MB, wet.pdb 93 MB. Imports rexruntimerd.dll + system DLLs only.
- Runtime paths: --game_data_root required (scripts/run.py passes ../game); user data/saves
  default to Documents\wet; optional config wet.toml next to the exe; cache in user dir.
- NOT launched yet.

## First launch (2026-10-25, user-approved)
- `scripts/run.py` -> wet.exe started, mapped guest memory, SDL input OK, then guest thread 4
  died after ~0.3 s: `[FATAL] Call to invalid or unregistered function at guest address
  0x83347B48` (REX_FATAL -> abort). No window content reached; exit code 3.
- Log noise before it: NtCreateFile FAILED for GAME:\__dummy.ini and GAME:\engine.ini
  (0xC000000F, file not found) - probably harmless probes; check against the disc listing later.
  SDL warns gamecontrollerdb.txt missing (affects controller mappings; revisit in input work).
- Crash reporter verified on a real guest crash: guest regs + back-chain correct
  (xstart -> 0x83044270 -> bctrl 0x83347B48), report 3.7 KB, dump 289 KB.
- Cause: xstart -> sub_83044270 walks the CRT static-init table 0x833A464C..0x833BD988
  (25,807 words, 3,206 non-zero); 2 entries were never emitted as functions.
- Wider check: tools/analysis/missing_ptr_funcs.py scans data sections for pointers into
  .text that look like function starts and are not registered (list from
  generated/default/wet_register.cpp). 75 candidates ->
  * 2 static initializers + 19 MI this-adjust thunks (subi r3,r3,N; b) + 1 virtual method
    (0x8332DE90, 13 refs) added to config/functions.toml (22 entries).
  * The rest are C++ EH continuation labels inside functions (follow `or r8,r8,r8` markers
    or a `bl`, use r31 as frame pointer) - not entry points, deliberately not added.
    If C++ exceptions misbehave later, revisit these (codegen has generate_exception_handlers).
- Gotcha: Python text files on Windows get CRLF; strip \r before shell comparisons.
- Ghidra (A2ALT language) cannot decode VMX128 opcodes (e.g. 1BE00774) - shows `??`.
- After fix: 73,380 functions registered, rebuild 0 warnings. Not relaunched yet.

## Milestone 1 reached: title screen (2026-10-25 03:47)
Launches 2-4 (user approved relaunch loop):
- Launch 2: no GPU emulation ("gpu_plugin not set"); the SDK cvar gpu_plugin defaults to ""
  (headless). WetApp::OnPreSetup now sets config.gpu_plugin = "xenos" if empty.
  Crash: unregistered 0x832654F8 (pointer built in code with lis+addi, no data ref).
- Launch 3: D3D12 up on RTX 3090; crash: unregistered 0x82CF88C0 (functor manager,
  predecessor instruction is `bctr`, which the scans did not accept as a function boundary).
- Fix approach (tools/analysis/):
  missing_ptr_funcs.py (data pointers) + missing_code_ptrs.py (lis+addi/ori pointers),
  predecessor blr/bctr/b/padding; drop EH continuations (r31-frame access, `or r8,r8,r8`);
  size_funcs.py-style check: accept only if every out-of-range branch targets a known function.
  Then two codegen-driven rejections:
  * conditional-branch targets (codegen can't make a `bc` into a tail call) - 6 removed;
  * func_extents.py: entries inside another generated function's label range (parent still
    branches across the cut -> "use of undeclared label") - 113 removed.
  config/functions.toml now has 111 entries; build clean.
- Launch 4: title screen "PRESS > TO BEGIN" renders correctly, ~717 MB RSS. Shader storage
  works (2 shaders cached). Audio opens on 'Headset Earphone (INZONE Buds - Chat)' 48 kHz 2ch
  - that is a chat/headset endpoint, check device choice in milestone 4.
- Known noise: IoDismountVolumeByFileHandle stub, VFS 'ShaderDumpxe:' no device,
  GAME:\__dummy.ini / engine.ini not found.
- Not yet tested: pressing Start / menus / gameplay / audio audibly / controller.
- run.py --no-crash-dialog sets WET_CRASH_NO_DIALOG=1 for unattended launches.

## Milestone 2 session 1: menus + first level (2026-10-25 03:53-04:08)
Launched with `run.py --no-crash-dialog -- --mnk_mode=true` (SDK keyboard->pad is OFF by
default; defaults: Return=Start, Space=A, Backspace=B, L=X, P=Y, E/O=RT, Q/I=LT, WASD=LS,
arrows=RS, Shift+arrows=D-pad). Driven with synthetic key presses (~0.2 s holds).
Verified working (screenshots):
- Bink intro video, middleware credits, title, auto-save notice.
- Save device prompt -> "Create New Game": save written to
  Documents\wet\B13EBABEBABEBABE\425307DB\00000001\WETSAVEGAME (+ Headers\00000001).
- Difficulty select, loading screen + hints, opening text, in-engine cutscene with subtitles.
- First level (Chinatown restaurant) gameplay: movement, jump, slide, shooting, enemy killed,
  style counter, glass bullet decals, tutorial popups. Rubi model renders fully (old Xenia
  showed only eyes/hair).
- Pause menu, Load Last Checkpoint (reload OK), Quit to Menu, main menu "Resume Game" loads
  the save.
- ~8 min total, no crash, no new log warnings. RSS 1.7-1.9 GB.
Not verified: audio audibly (I can't hear it), controller, anything past the first tutorial
section, perf/frame pacing (milestone 4).
Observations:
- Log file stayed at 25 lines during the whole session: check flush behaviour
  (log_flush_interval default) before relying on logs for diagnostics.
- Blind scripted play is slow (tutorial popups pause the game); deeper coverage needs
  milestone 3 input record/replay or human play.

## 60 fps (2026-10-25)
Measurement tooling:
- SDK bug: --perf_log_csv is defined but SetCsvLogPath() is never called. WetApp::
  OnPostInitLogging applies it (app-side, no SDK patch). CSV is opened exclusively; read it
  after the game exits. One row per guest swap (XE_SWAP), frame_time_us = host time between swaps.
- tools/analysis/frametimes.py <csv> [--last N] -> mean/median/p95/p99 + vblanks/frame histogram.
Baseline: locked 30 fps (5,852 frames, 99.9% exactly 2 vblanks; gameplay p99 35.9 ms).
Cap location (D3D, statically linked in the game):
- vblank interrupt cb 0x8305E5B8 -> sub_83066A10: vblank counter device+16548, swap ring
  read/write idx +16700/+16704, entries {frontbuffer,+16572; target vblank,+16576}.
- sub_83066B28 (swap callback): packed arg bits 8-11 = vblanks/swap; target = max(last+N, now).
- sub_83066D80 (present): device+13596 = PresentationInterval (0/1->1, 2->2, 4->3) -> r11.
Fix: config/hooks.toml midasm hook WetPresentIntervalHook @0x83066E70 (r11 2 -> 1),
  src/patches/framerate.cpp, cvar wet_unlock_60fps (default true).
Results (gameplay, last 600 frames): 30 fps -> 60.0 fps, 100% 1-vblank frames,
  median 16.63 ms, p99 19.5 ms. Overall 11,025 frames 59.9 fps.
Game-speed check (A/B, same save, same inputs, wet_unlock_60fps true vs false):
- Restaurant cutscene: identical shot + subtitle at +10/+20/+30/+40 s in both modes.
- Gameplay: holding W 2.0 s from level start ends at the same spot (between the ice
  dragons, next to the same enemy) in both modes -> movement uses real delta time.
- Not checked: physics edge cases, animation/blend timing, scripted timers, Bink video speed.
Frame pacing at 60: mean 16.66 but 13-20 ms spread, 29.6% > 17.5 ms, 179 long->short pairs
  -> SDK vblank thread Sleep(1) jitter; testing patch 2 (high-res timer).
Patch 2 A/B (title/intro idle, no input, frames 1800-4200, 80 s runs, cvar vblank_precise_timer):
| run | precise | stdev | p1 | p99 | >17.5ms |
| 1 | false | 1.227 | 13.66 | 19.99 | 28.0% |
| 1 | true  | 0.923 | 15.00 | 18.52 | 22.9% |
| 2 | false | 1.416 | 13.51 | 20.01 | 30.5% |
| 2 | true  | 1.039 | 14.64 | 18.66 | 26.7% |
-> kept (stdev -25%, p99 20.0 -> 18.6 ms). Mean 16.666 ms either way.
Remaining ~25% of frames >0.8 ms off: not the vblank thread; suspect CP swap-packet timing
(frame_time_us is measured when the CP executes XE_SWAP). Investigate in milestone 4 with
host-present timestamps.
Gotchas this session:
- computer-use open_application("Wet") launches a NEW wet.exe without args (error
  "--game_data_root was not provided"); never use it to focus. Use WScript.Shell AppActivate.
- TextInputHost (Windows IME host) can grab the foreground and block synthetic input.
- taskkill is async: wait for the process to disappear before run.py (its guard refuses).

## Mouse look (2026-10-25)
- Why the mouse "didn't work": SDK v0.10.0 has mnk_mode and mnk_mouse both default false.
  src/input_defaults.cpp sets mnk_mode=true, mnk_mouse=true, mnk_mouse_to_stick=false unless
  the user set them (cvar Source::kDefault check; runs in OnPostInitLogging, after config + CLI).
- SDK mouse->stick (delta*200*sens per poll) felt slow + joystick-like: per-poll deltas of 2-4
  counts -> stick ~600/32767 (inside the game's dead zone), then integrated as angular velocity.
- Synthetic SendInput relative moves are NOT delivered once SDL relative mode is on (real mouse
  is). Can't automate mouse tests that way.
Input chain (found with temporary hooks logging guest lr + the memtool watchpoint):
- XInput poll: vtable 0x82316B14 -> sub_82BF2AA0 (Controller::Update): X_INPUT_STATE on stack,
  floats at dev+8/+12 (LX/LY), +16/+20 (RX/RY).
- InputDevice::GetValue(ch) sub_82BF1CC0 (vtable slot 1): float at dev+(ch+2)*4; ch>=1000 remap.
- InputAction::GetValue sub_82B0ADD0 (binding list at this+16).
- GetLeftStick sub_829A2F10 / GetRightStick sub_829A2FE8 (this, vec2* out).
- Player control snapshot sub_829B8078: right stick -> player+164/+168, left -> +172/+176,
  sub_829A64B8 = camera-relative/deadzone transform (invert flag bytes).
- Camera update sub_829B1960: per axis sub_829AB470(angle, max, min, speed, stick, out):
  angle += dt(timer vfn) * speed * stick, then sub_829A8168 clamp/wrap. Yaw result r1+84,
  pitch r1+80, pitch clamp sub_829AEFC0 afterwards. Only caller of sub_829AB470.
Fix: hook WetMouseLookHook @0x829B1AA8 (r1,f30=yaw speed,f27=pitch speed) adds raw counts *
  wet_mouse_sensitivity (deg/count, default 0.05) to yaw/pitch, direction = sign of game speed
  (same as stick), wet_mouse_invert_y, deltas older than 0.25 s dropped (menus/cutscenes).
  wet_raw_mouse=false restores SDK stick emulation. Controller/arrow-key camera unaffected (verified).
Tools added: src/debug/memtool.cpp (--wet_memtool): dump.req (all committed guest memory),
  poke.req, watch.req (PAGE_GUARD data watchpoint, logs host fn = sub_<guest addr>);
  tools/analysis/memdiff.py, show_func.py. Note dumps are 1.6 GB each (E:\WET\work\memtool).
- NOT yet verified: actual mouse feel / sensitivity / pitch direction with a real mouse.

## Mouse look, final approach (2026-10-25)
- Writing yaw/pitch directly in the camera update (0x829B1AA8) did NOT work: with a zero stick the
  camera's auto-follow re-derives the angles every frame (hook diagnostics: our delta never
  survived to the next frame) and other code uses a non-zero stick snapshot to stop auto-follow.
- Working: WetMouseLookHook @0x829B812C (r29 = player) in the control snapshot sub_829B8078, after
  sub_829A64B8 processed the right stick: stick += mouse_rad / (dt * |speed|), speed from
  [player+500]+80 (yaw) / +92 (pitch), dt = real time since last poll. Both snapshot axes are
  negated relative to the raw stick -> add_x = -dx..., up = +dy (user-verified directions).
  Feels 1:1 to the user at 0.05 deg/count. Caveat: game slow-mo scales its dt, real dt is used here.
- Lesson: I claimed the first mouse version worked without being able to test a real mouse.
  Synthetic SendInput moves don't reach SDL relative mode; real-mouse tests need the user.

## Controls menu (F1), 2026-10-25 - user-tested OK
- src/ui/controls_menu.{h,cpp}: Keyboard & Mouse tab (keybind_* with key/mouse-button capture,
  Bind/Add/Clear beside the binding, bare Shift/Ctrl/Alt, mouse sensitivity/invert/raw toggle),
  Controller tab (wet_pad_* remap via src/input/pad_remap.cpp, press-a-button capture, swap
  sticks, invert right Y), editable action names wet_action_* (defaults from user: RB Shoot,
  LB Wall Run, A Jump, B Slide, X Melee, Y Action, Back Move List, Start Pause), Save -> wet.toml.
  Large window (70%x85% of screen, 1.5x font). Open/close: F1 (cvar bind_wet_controls).
- SDK patches 4 (SetPadStateFilter) and 5 (bare-modifier binds, plain keys ignore modifiers
  unless the key is also used with a prefix, SetMnkInputSuppressed for menus).
- Controller (Xbox One pad) works through SDL backend; merged with keyboard/mouse on player 1.
- Not verified: whether remapped pad buttons apply in menus that read keystroke events
  (XamInputGetKeystrokeEx) rather than GetState.

## Pause point (2026-10-25 night) - where to resume
Done: M1 (title screen), 60 fps + frame pacing patch, raw mouse look, controller support, F1
Controls menu. User has played into the Chinatown alley section without crashes.
Open items:
- Milestone 2 (crash fixes): continue via user playtesting; crash reports in out/build/.../crashes.
- Milestone 3: input record/replay, save backup/restore, section select (not started).
- Milestone 4: remaining frame-time jitter (~25% of frames >0.8 ms off), audio latency logging,
  audio device choice (currently opens 'Headset Earphone (INZONE Buds - Chat)').
- Proper KB/M default layout once the user knows all actions (action names now in the menu).
- Cleanup: remove the unused SDK mouse diag leftovers if any; consider Release build later.
- Process: never taskkill the game inside a build command without asking (happened once).

## Session 2026-10-26: frame pacing, controls save, lock-on mouse, aim assist, camera
- Frame rate: SDK patches 6-8 (perf counters, no wait on async pipeline creation, present
  pacing at the publish point). Busy scenes >~1600 draw calls still drop (~9 us/draw of
  translation cost); cp_thread_cpu_us is logged (logs/perf-play9.csv) but not analyzed yet.
- Controls save: SDK SaveConfig only wrote launch flags -> explicit ControlsMenu::Save() writes
  all input cvars + non-default runtime/config cvars to wet.toml. Menu exists only while open
  (a permanent dialog made the SDK present ~4000 times/s).
- Lock-on / slow-mo mouse drop: the aim paths read the right stick through their own calls, so
  the per-frame mouse stick value is mirrored into their stack vectors (Mirror96/88/80 hooks).
- Aim assist: WeaponArmManager+316 bit 0x800000 (IsAimAssistEnabled) cleared every frame while
  wet_aim_assist is off (user prefers it fully gone; the in-game option is overridden).

### Camera orbit while strafing (open areas) - FIXED, user-verified 2026-10-26 05:10
Symptom: walking left/right with the mouse still rotated the camera around the character; the
mouse had to counteract it. Four earlier hooks (lock branch, basis timer, moving block, orbit
replacement) did nothing because the rotation is not in any camera controller.
Method that worked: memtool watchpoint (cam+432..+496) named the only writer of the stored
yaw (the camera update's own final store), then four temporary hooks logged yaw at each stage
of sub_829BA128 (after boom->angles, after sub_829B9490, after sub_829B1960, at the store) with
the basis rows and the boom. Result: yaw was identical through every stage; the boom already
pointed elsewhere at the start of the next frame. Two causes:
1. sub_829AEE68 builds the basis the angles live in from the character's transform (both
   branches; there is no world-basis branch), so the basis turns with the heading and the
   stored, basis-relative angle is smoothed into a rotated basis.
   -> WetCameraBasisFlagHook (0x829AEEB8, r10=0) + WetCameraFollowHook (0x829AEECC, f12 huge,
   rewrite rotation rows of the copied transform with the identity): world-relative angles.
2. Trailing camera: cam+192 is the smoothed target position (sub_829A7F68 lerp at params+560
   rates); at frame start the update subtracts this frame's target movement from the boom
   (v126 = new - old cam+192; v1 = boom - v126 at 0x829BA328) before boom->angles, i.e. the
   camera stays where it was in the world and the boom swings behind the motion.
   -> WetCameraTrailHook (0x829BA328, v126 = 0): the boom direction persists frame to frame.
Both gated on wet_raw_mouse && !wet_camera_follow. The log after the fix shows yaw flat during
the whole strafe and changing only with mouse input.
Camera update structure (sub_829BA128), for later: control snapshot sub_829B8078; timer
cam+464; basis r1+144 (sub_829AEE68); smoothed offsets cam+192/+208 (sub_829A7F68); boom
cam+80 -> angles r1+112/116/120 (sub_829AB1A8, yaw not reloaded from +476 since params+652 is
0, pitch reloaded from +480 since params+644 is set); controllers chosen by params flags and
sub_828ED770 checks: sub_829B8A68, sub_829B90A8, sub_829B8D28, sub_829B9490 (open area, then
sub_829B1960 stick camera on top), sub_829B1960 (integrator sub_829AB470 angle += dt*speed*
stick via sub_829A8168 clamp); angles -> boom (sub_829A8078), stored to +476/+480/+484 and
cam+80 at 0x829BA898..8C0. Address listing of a generated function: count the '// ' comment
lines from the function start (labels verify the count); scratch script /tmp/addr.py did this.
- memtool watch gotcha: ExceptionInformation[1] is the faulting address, so a 16-byte vector
  store starting below the watched range is missed; watch a wider range. The handler logs each
  unique rip once.

## Session 2026-10-30: display modes, vsync, frame rate (GPU thread)
- Display tab (F1): exclusive fullscreen (SDK patch 9: the runtime DLL has its own SDL3, so
  the exe cannot touch the window through SDL), borderless, windowed; render scale 1x/2x/3x
  (resolution_scale) and output resolution (resolution preset), both next-launch.
- Host vsync (SDK patch 10): sync interval 0 without the tearing flag, 4-buffer waitable swap
  chain with frame latency 1, UI-thread paints keyed to guest frame publication. Three earlier
  variants starved the guest (13-53 fps): the SDK's permanent toast drawer re-arms a repaint
  after every paint, and every vsync'd UI present stalled the GPU queue the guest shares
  (command_buffer_stalls = 1/frame, cp_fence_wait_us ~11 ms). Verified: 60 fps, zero stalls.
  Note the user's desktop is 60 Hz, so exclusive mode and vsync pick 60 Hz.
- Frame drops in busy scenes (1500-2000+ draws) are CPU-bound on the SDK's "GPU Commands"
  thread (cp_thread_cpu_us tracks frame time; GPU waits 0). Sampling profiler added to the
  memtool (profile.req) with SDK PDBs from third_party/rexglue-sdk/out/win-amd64/RelWithDebInfo.
  Profile at ~1160 draws: UploadRanges 34% (memcpy of guest memory to upload buffers),
  page protection 11%, NVIDIA driver 16%, register writes 12%, texture cache ~8%.
- Upload volume: 50-65 MB/frame in 2000-2900 ranges (counters shared_mem_upload_*). Causes:
  the game rewrites ~45 MB of dynamic vertex data per frame (real), plus 256 KB invalidation
  blocks (SDK patch 11 makes it a cvar; 4 KB = -22% upload, ~700 access violations/frame with
  no visible game-thread cost -> port default 12 in WetApp::OnPreSetup).
- Parallel memcpy (patch 12) gained only 0.5 ms: the copy into write-combined upload memory is
  bandwidth-bound (12.6 GB/s). Timers: upload 5.6 ms = copy 3.55 + protect 1.15 + pool 0.9.
- Zero-copy (patch 13): D3D12 heap opened over the guest physical memory
  (OpenExistingHeapFromAddress works on the section view; the buffer must be placed with
  ALLOW_CROSS_ADAPTER + GENERIC_READ, heap flags 0x421), uploads are CopyBufferRegion from
  it. Result at 1600-2200 draws: GPU thread 18.5 -> 14.1 ms, frame 19.0 -> 17.6 ms; 1200-1600
  draws now a flat 60. Upload cost left: ~1 ms of page re-protection. User saw no artifacts.
- Next for frame rate: re-profile on the zero-copy build; candidates are the driver/command
  recording share, register-write path, texture cache hashing, and the protect calls.
- Async submission (SDK patch 14, d3d12_async_submit): the deferred command list replay
  measured 5.2 ms per heavy frame (cp_replay_us, ~6.7 submissions/frame; ExecuteCommandLists
  itself 0.3 ms). A "GPU Submit" worker replays/executes/signals each submission in order while
  the GPU Commands thread records the next; drained at swaps and shutdown. Result at 1600-2200
  draws: GPU thread 15.7 -> 10.8 ms, frame 18.3 -> 17.1 ms (54.7 -> 58.3 fps). The remaining
  excess is GPU-side (cp_fence_wait_us 2.6 ms) at 2x render scale. Batched page protection
  (tried as a patch) measured nothing and was dropped. No visual issues in a few minutes of play.
- Streaming hitches (F2 marker + texture counters): a burst of ~262 new textures at an area
  transition cost a 96 ms frame; CreateCommittedResource measured 221 us per texture (51 ms of
  it). SDK patch 15 places textures in pooled 64 MB heaps (first-fit free lists, blocks freed on
  texture destruction, which the cache only does after the last GPU use): creation 28 us avg,
  burst frame 96 -> 44 ms. What is left of the hitch: the replay of that submission (16 ms,
  per-texture barriers/dispatch/copy) and the GPU untiling ~50 MB through the single scratch
  buffer (two frames of 40-52 ms fence waits). Batching the loads would be the next step.
- At 2200+ draws the limit is now the D3D12 recording cost on the GPU Submit thread (~7 us per
  draw, 15-27 ms replay per frame at 2x render scale) plus GPU time at 2x.
- F2 writes a test marker (logs/marks.txt: n, time, perf CSV row); tools/analysis/wait_mark.py
  waits for it, so test runs no longer need a typed "done".
- GPU timestamps (gpu_submission_us, SDK patch 16): at 2x render scale the GPU needs 18.3 ms per
  frame at 1600-2200 draws (12.7 ms even at 300-800), so heavy scenes are GPU-bound at 2x; at
  1x 13.7 ms and every draw band holds 60 (20k frames, 5 frames > 33 ms). The first area
  transition's texture burst is real GPU work: ~210 ms at 2x, ~120 ms at 1x, over 3-5 frames;
  a later, larger burst (199 textures, 49 MB) costs nothing extra, so it is about what those
  textures are, not how many (texture_load_host_bytes added to check for expansion on load).
  Batched texture loads measured worse on the GPU (99 -> 177 ms) and were dropped. The plain
  512 MB shared-memory buffer (no tile mappings) saved 13 ms on the burst frame: port default.
  The SDK uses the ROV EDRAM path (render_target_path_d3d12 unset); the RTV path is the next
  thing to try for the 2x GPU cost.
- Hair trailing the body by a frame (user noticed 2026-10-30 evening): caused by zero-copy
  uploads (patch 13) - the GPU read the per-frame hair vertex data when the copy executed, a
  frame or more behind the game thread with async submission. Confirmed by the flag; default is
  now off (CPU snapshot copy at record time, ~3.5 ms on the heaviest frames, 3 copy threads).
  Lesson: "the console GPU also reads later" was wrong for this data; the game relies on the
  emulator's snapshot timing. RTV render-target path: no GPU gain over ROV at 2x (19.0 vs 17.7 ms).
- 2x render scale GPU cost: gamma_render_target_as_unorm16=false and native_2x_msaa=false
  changed nothing (GPU 19.0 vs 18.9 ms at 1600-2200 draws), like the RTV path. The GPU time at
  2x is the game's own shading at 4x pixels; 1x holds a locked 60. Remaining 2x options are
  structural (live resolution switching) - not pursued.
