"""Launch the recompiled game (only run when the user has said to).

Usage:
    python scripts/run.py [--config RelWithDebInfo] [--game E:\\WET\\game] [-- extra cvars...]

Passes --game_data_root and a timestamped --log_file under logs/.
Refuses to start a second copy while the game is already running.
"""
import argparse
import os
import subprocess
import sys
import time
from pathlib import Path

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build  # noqa: E402  (shares ROOT and the running-game check)


def main():
    p = argparse.ArgumentParser()
    p.add_argument("--config", default="RelWithDebInfo")
    p.add_argument("--game", default=str(build.ROOT.parent / "game"))
    p.add_argument("--no-crash-dialog", action="store_true",
                   help="write crash report/dump without the blocking message box")
    p.add_argument("extra", nargs=argparse.REMAINDER)
    args = p.parse_args()

    running = build.running_game_processes()
    if running:
        sys.exit(f"run.py: game already running: {', '.join(running)}")
    exe = build.ROOT / "out" / "build" / f"win-amd64-{args.config.lower()}" / "wet.exe"
    if not exe.exists():
        sys.exit(f"run.py: {exe} not built")
    if not (Path(args.game) / "default.xex").exists():
        sys.exit(f"run.py: no default.xex in {args.game}")

    logs = build.ROOT / "logs"
    logs.mkdir(exist_ok=True)
    log_file = logs / time.strftime("wet-%Y%m%d-%H%M%S.log")
    extra = [a for a in args.extra if a != "--"]
    cmd = [str(exe), f"--game_data_root={args.game}", f"--log_file={log_file}", *extra]
    print("+", " ".join(cmd), flush=True)
    env = dict(os.environ)
    if args.no_crash_dialog:
        env["WET_CRASH_NO_DIALOG"] = "1"
    sys.exit(subprocess.run(cmd, cwd=exe.parent, env=env).returncode)


if __name__ == "__main__":
    main()
