# SDK patches

All local changes to ReXGlue SDK `v0.10.0` live in `rexglue-v0.10.0.diff`
(applied to `third_party/rexglue-sdk` by `scripts/build.py sdk`).

| # | File | Why |
|---|---|---|
| 1 | thirdparty/CMakeLists.txt | `libmspack/cabextract/mspack/*` are git symlinks; on Windows without `core.symlinks` they check out as one-line text files and `lzxd.c` fails to compile. Point `MSPACK_DIR` at the real `libmspack/libmspack/mspack`. |

Regenerate after editing the SDK: `git -C third_party/rexglue-sdk diff > patches/rexglue-v0.10.0.diff`
| 2 | src/graphics/graphics_system.cpp | Guest vblank thread polled with `Sleep(1)` and caught up late: 60 fps frames spread 13-20 ms (30% > 17.5 ms). Windows: wait on a high-resolution waitable timer until 0.5 ms before each vblank deadline, then spin. Kept only if measurements improve (see NOTES.md). |
| 3 | include/rex/input/mnk/mnk_input_driver.h, src/input/mnk/mnk_input_driver.cpp | Raw 1:1 mouse look: `TakeRawMouseDelta()` returns relative mouse counts accumulated while captured; cvar `mnk_mouse_to_stick` (default true = SDK behaviour) stops feeding the mouse into the right stick. Wet sets it false and applies counts to the camera itself. |
| 4 | include/rex/input/input_system.h, src/input/input_system.cpp | `SetPadStateFilter()`: optional callback applied to each real (non-synthetic) pad's `X_INPUT_STATE` before the per-user merge. Wet uses it for controller button remapping and "press a button" capture. |
| 5 | include/rex/input/mnk/mnk_input_driver.h, src/input/mnk/mnk_input_driver.cpp | Modifier keys: a bare "Shift"/"Control"/"Alt" bind fires while that key is held (SDK could never fire it), and plain keys ignore held modifiers unless the same key is also bound with a modifier prefix somewhere (keeps arrows vs Shift+arrows working, but lets Shift-aim + W-move work). `SetMnkInputSuppressed()`: neutral keyboard/mouse pad while still updating mouse capture, so a menu can free the cursor immediately. |
| 6 | include/rex/perf/counter.h, src/core/perf/counter.cpp, src/graphics/d3d12/pipeline_cache.cpp | Diagnostics: per-frame `pipeline_wait_us` (GPU thread blocked in `EndSubmission` on queued pipeline creation) and `shader_translate_us` counters in the perf CSV; CSV opened share-readable on Windows. |
| 7 | src/graphics/d3d12/pipeline_cache.cpp | `EndSubmission` no longer waits for queued pipeline creation (nor creates pipelines on the GPU thread) when `async_shader_compilation` is on: the command processor already skips draws whose pipeline is not ready, so the wait only stalled frames (measured 100-400 ms per burst of new shaders, 59% of stutter time). Set `--async_shader_compilation=false` for the original blocking behaviour. |
