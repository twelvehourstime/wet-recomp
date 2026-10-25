# SDK patches

All local changes to ReXGlue SDK `v0.10.0` live in `rexglue-v0.10.0.diff`
(applied to `third_party/rexglue-sdk` by `scripts/build.py sdk`).

| # | File | Why |
|---|---|---|
| 1 | thirdparty/CMakeLists.txt | `libmspack/cabextract/mspack/*` are git symlinks; on Windows without `core.symlinks` they check out as one-line text files and `lzxd.c` fails to compile. Point `MSPACK_DIR` at the real `libmspack/libmspack/mspack`. |

Regenerate after editing the SDK: `git -C third_party/rexglue-sdk diff > patches/rexglue-v0.10.0.diff`
