"""Package the installer release zip (contains no game files and no recompiled code).

Usage:
    python scripts/make_release.py <version> [--out DIR]

Produces <out>/wet-recomp-<version>.zip with:
    Install-Wet.bat, Install-Wet.ps1, README.txt   (from installer/)
    source/                                        (git archive of HEAD: tracked files only)
The working tree must be clean so the zip matches a commit.
"""
import argparse
import io
import subprocess
import sys
import tarfile
import zipfile
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
FORBIDDEN_SUFFIXES = {".iso", ".xex", ".xexp", ".pak", ".xpr", ".xma", ".bik", ".exe", ".dll"}


def git(*args):
    return subprocess.run(["git", *args], cwd=ROOT, check=True, capture_output=True).stdout


def main():
    p = argparse.ArgumentParser()
    p.add_argument("version")
    p.add_argument("--out", default=str(ROOT / "out" / "release"))
    args = p.parse_args()

    if git("status", "--porcelain").strip():
        sys.exit("make_release.py: working tree is not clean; commit first")
    commit = git("rev-parse", "--short", "HEAD").decode().strip()
    tar_bytes = git("archive", "--format=tar", "HEAD")

    out_dir = Path(args.out)
    out_dir.mkdir(parents=True, exist_ok=True)
    zip_path = out_dir / f"wet-recomp-{args.version}.zip"
    top = f"wet-recomp-{args.version}"
    count = 0
    with zipfile.ZipFile(zip_path, "w", zipfile.ZIP_DEFLATED) as z:
        for name in ("Install-Wet.bat", "Install-Wet.ps1", "README.txt"):
            # Windows line endings: cmd.exe and Notepad users get these files directly.
            text = (ROOT / "installer" / name).read_text(encoding="ascii")
            text = text.replace("\r\n", "\n").replace("\n", "\r\n")
            z.writestr(f"{top}/{name}", text.encode("ascii"))
        with tarfile.open(fileobj=io.BytesIO(tar_bytes)) as t:
            for member in t.getmembers():
                if not member.isfile():
                    continue
                if Path(member.name).suffix.lower() in FORBIDDEN_SUFFIXES:
                    sys.exit(f"make_release.py: refusing to package {member.name}")
                z.writestr(f"{top}/source/{member.name}", t.extractfile(member).read())
                count += 1
        z.writestr(f"{top}/source/VERSION.txt", f"{args.version} ({commit})\n")
    print(f"{zip_path}  ({count} source files, commit {commit}, "
          f"{zip_path.stat().st_size / 1024:.0f} KB)")


if __name__ == "__main__":
    main()
