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
