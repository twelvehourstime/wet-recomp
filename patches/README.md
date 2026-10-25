# SDK patches

All local changes to ReXGlue SDK `v0.10.0` live in `rexglue-v0.10.0.diff`
(applied to `third_party/rexglue-sdk` by `scripts/build.py sdk`).

| # | File | Why |
|---|---|---|
| 1 | thirdparty/CMakeLists.txt | `libmspack/cabextract/mspack/*` are git symlinks; on Windows without `core.symlinks` they check out as one-line text files and `lzxd.c` fails to compile. Point `MSPACK_DIR` at the real `libmspack/libmspack/mspack`. |

Regenerate after editing the SDK: `git -C third_party/rexglue-sdk diff > patches/rexglue-v0.10.0.diff`
| 2 | src/graphics/graphics_system.cpp | Guest vblank thread polled with `Sleep(1)` and caught up late: 60 fps frames spread 13-20 ms (30% > 17.5 ms). Windows: wait on a high-resolution waitable timer until 0.5 ms before each vblank deadline, then spin. Kept only if measurements improve (see NOTES.md). |
