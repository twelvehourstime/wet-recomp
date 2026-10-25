# wet-recomp

An unofficial native Windows port of **Wet** (Xbox 360, 2009) produced by static
recompilation with the [ReXGlue SDK](https://github.com/rexglue/rexglue-sdk) (pinned to `v0.10.0`).

**This repository contains no game files.** No ISO, XEX, archives, extracted data or
generated recompiled code. You must bring your own legally obtained copy of the game.

Status: early development. Not playable yet.

Planned: XInput controller support, mouse and keyboard controls.

License: GPL-2.0-or-later (to be added at publish time).

## Getting the game files (your own copy)

Extract your Xbox 360 disc image into a `game` folder **next to** this repository
(the manifest refers to `../game/default.xex`):

```
python tools/xdvdfs_extract.py extract path\to\Wet.iso ..\game
```

Expected: title ID `425307DB`, `default.xex` 22,364,160 bytes, 13,355 files.
