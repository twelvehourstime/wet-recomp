Wet - native PC port (wet-recomp)
=================================

This package does NOT contain the game. It turns your own Xbox 360 copy of Wet into a
native Windows game, built on your computer.

You need
--------
- Windows 10 or 11 (64-bit) and a DirectX 12 graphics card.
- Your Wet Xbox 360 disc image as an .iso file (for example, a dump of your own disc).
- About 15 GB of free disk space (plus about 8 GB for build tools if they are not installed yet) and an internet connection.

How to install
--------------
1. Extract this whole zip to a short folder path, for example C:\Games\Wet.
2. Double-click Install-Wet.bat.
3. Pick your Wet .iso file when asked.
4. If build tools are missing, the installer lists them and asks before installing them
   with winget (Git, Python, CMake, Ninja, LLVM, Visual Studio 2022 Build Tools).
   Windows may ask for administrator approval.
5. Wait for the build (10-30 minutes the first time).
6. Start the game with the "Wet" shortcut on your desktop.

Press F1 in game for controls, display mode, vsync and resolution settings.
Settings are saved in Wet\wet.toml.

After installing, the folder contains:
  Wet\      the game - this is all you need to play
  game\     files extracted from your disc (the game reads these; keep them)
  source\   the port's source code and build files (can be deleted to save space,
            but then updating needs a fresh install)

If something goes wrong, install.log in this folder has the details. Please include it
when reporting a problem.

Source code, issues and updates: https://github.com/twelvehourstime/wet-recomp
License: GPL-2.0-or-later. Wet is a trademark of its respective owners; this project is
not affiliated with them.
