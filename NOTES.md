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
