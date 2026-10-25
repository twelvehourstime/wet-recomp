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
