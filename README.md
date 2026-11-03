# wet-recomp

An unofficial native Windows port of **Wet** (Xbox 360, 2009), built by statically
recompiling the game's executable with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk)
(pinned to `v0.10.0`).

**This repository contains no game files.** No ISO, XEX, archives, extracted data or
generated recompiled code. You need your own legally obtained copy of the Xbox 360 disc.
The build turns *your* copy into a Windows executable on your machine.

## Status

Playable. Developed and tested on Windows 11 with an NVIDIA RTX 3090; not every level has
been played through yet. Bug reports are welcome.

What the port adds over the original:

- Native x64 Windows executable with Direct3D 12 rendering.
- Locked 60 fps with an optional low-latency vsync.
- Render scale 1x / 2x / 3x (720p, 1440p, 2160p internal) and 720p / 1080p / 1440p output.
- Fullscreen, borderless and windowed modes.
- Mouse and keyboard with raw mouse look, and a fix for the camera drifting while strafing.
- XInput and SDL controllers (Xbox, DualSense and others).
- In-game controls menu (F1) to rebind keys, mouse and controller buttons.

Known issues:

- The first area transition in a level can hitch for a few frames while textures stream in.
- 3x render scale is untested.

## Requirements

- Windows 10 or 11, x64, with a Direct3D 12 GPU.
- [Git](https://git-scm.com/), [Python 3](https://www.python.org/), [CMake](https://cmake.org/) 3.25+
  and [Ninja](https://ninja-build.org/) on `PATH`.
- [LLVM/Clang](https://github.com/llvm/llvm-project/releases) (tested with 22.1). The build
  looks in `C:\Program Files\LLVM\bin`; set `LLVM_BIN` if it is installed elsewhere.
- Visual Studio 2022 or its Build Tools with the "Desktop development with C++" workload
  (Clang uses its Windows SDK and runtime libraries).
- About 10 GB of free disk space.

## Building

1. **Clone this repository** and open a terminal in it.

2. **Extract your disc image** into a `game` folder next to the repository (the build
   reads `../game/default.xex`):

   ```
   python tools/xdvdfs_extract.py extract path\to\Wet.iso ..\game
   ```

   Expected: title ID `425307DB`, `default.xex` 22,364,160 bytes, 13,355 files.

3. **Build the SDK.** This clones ReXGlue `v0.10.0` into `third_party/` and applies the
   port's SDK patches from `patches/`:

   ```
   python scripts/build.py sdk
   ```

4. **Build the game.** This recompiles `default.xex` into C++ and compiles it:

   ```
   python scripts/build.py game
   ```

5. **Run it:**

   ```
   python scripts/run.py
   ```

   The executable is `out/build/win-amd64-relwithdebinfo/wet.exe`. It needs
   `--game_data_root` pointing at the `game` folder, which `run.py` passes for you.

## Settings

Press **F1** in game for the controls and display menu. Settings are saved to `wet.toml`
next to `wet.exe`. Every option can also be set on the command line, for example:

```
python scripts/run.py -- --resolution_scale=2 --host_vsync=true
```

## How it works

ReXGlue translates the game's PowerPC code into C++ ahead of time and supplies a runtime
for the Xbox 360 kernel, audio and a Xenos GPU emulator. This repository holds only the
port's own code: function and hook definitions (`config/`), game-specific patches and UI
(`src/`), SDK fixes as one reviewed diff (`patches/`, documented in `patches/README.md`),
and analysis tools (`tools/`). `NOTES.md` is the development log.

## License

The port's own code is licensed under the GNU General Public License, version 2 or (at your
option) any later version. See `LICENSE`.

`patches/rexglue-v0.10.0.diff` modifies the ReXGlue SDK, which is BSD-3-Clause licensed
(portions derived from Xenia); those changes are offered under the SDK's license.

Wet is a trademark of its respective owners. This project is not affiliated with or
endorsed by them, and it does not include or distribute any of the game's files.
