"""Build driver for wet-recomp.

Usage:
    python scripts/build.py sdk [--config RelWithDebInfo] [--clean]

Refuses to run while the game (or Xenia) is running, so a build never
replaces binaries or generated code under a live process.
"""
import argparse
import csv
import io
import os
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
SDK_DIR = ROOT / "third_party" / "rexglue-sdk"
SDK_REPO = "https://github.com/rexglue/rexglue-sdk.git"
SDK_TAG = "v0.10.0"
SDK_PRESET = "win-amd64"
LLVM_BIN = Path(r"C:\Program Files\LLVM\bin")

# Lower-case image names that mean "the game is running".
GAME_PROCESSES = {"wet.exe", "wet-recomp.exe", "xenia_canary.exe", "xenia.exe"}


def running_game_processes():
    out = subprocess.run(["tasklist", "/fo", "csv", "/nh"],
                         capture_output=True, text=True, check=True).stdout
    names = {row[0].lower() for row in csv.reader(io.StringIO(out)) if row}
    return sorted(names & GAME_PROCESSES)


def refuse_if_game_running():
    found = running_game_processes()
    if found:
        sys.exit(f"build.py: refusing to build, game is running: {', '.join(found)}")


def tool_env():
    env = os.environ.copy()
    env["PATH"] = str(LLVM_BIN) + os.pathsep + env["PATH"]
    return env


def run(cmd, **kw):
    print("+", " ".join(str(c) for c in cmd), flush=True)
    subprocess.run(cmd, check=True, env=tool_env(), **kw)


def ensure_sdk_source():
    """Clone the pinned SDK if missing and apply patches/rexglue-<tag>.diff once."""
    if not SDK_DIR.exists():
        run(["git", "clone", "--branch", SDK_TAG, "--depth", "1", "--recurse-submodules",
             "--shallow-submodules", SDK_REPO, str(SDK_DIR)])
    patch = ROOT / "patches" / f"rexglue-{SDK_TAG}.diff"
    already = subprocess.run(["git", "apply", "--reverse", "--check", str(patch)],
                             cwd=SDK_DIR, capture_output=True).returncode == 0
    if not already:
        run(["git", "apply", str(patch)], cwd=SDK_DIR)


def build_sdk(args):
    ensure_sdk_source()
    build_dir = SDK_DIR / "out" / "build" / SDK_PRESET
    if args.clean and build_dir.exists():
        shutil.rmtree(build_dir)
    run(["cmake", "--preset", SDK_PRESET,
         f"-DCMAKE_C_COMPILER={LLVM_BIN / 'clang.exe'}",
         f"-DCMAKE_CXX_COMPILER={LLVM_BIN / 'clang++.exe'}"], cwd=SDK_DIR)
    run(["cmake", "--build", str(build_dir), "--config", args.config,
         "--target", "install", "--parallel"], cwd=SDK_DIR)


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    s = sub.add_parser("sdk", help="configure, build and install the ReXGlue SDK")
    s.add_argument("--config", default="RelWithDebInfo")
    s.add_argument("--clean", action="store_true")
    s.set_defaults(func=build_sdk)
    args = p.parse_args()
    refuse_if_game_running()
    args.func(args)


if __name__ == "__main__":
    main()
