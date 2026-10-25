"""Derive function extents from generated code: start = DEFINE_REX_FUNC(sub_X) address,
end >= highest loc_XXXXXXXX label inside its body. Then report config [functions] entries
that fall inside another function's labelled range (i.e. they would split a function whose
body still branches across the cut).

Usage: python tools/analysis/func_extents.py <generated/default> <config/functions.toml>
"""
import re
import sys
from pathlib import Path

FUNC = re.compile(r"^DEFINE_REX_FUNC\(([A-Za-z0-9_]+)\)")
LABEL = re.compile(r"^loc_([0-9A-F]{8}):")
REG = re.compile(r"SetFunction\(0x([0-9A-F]{8}), ([A-Za-z0-9_]+)\)")


def extents(gen_dir):
    name_to_addr = {}
    for m in REG.finditer((gen_dir / "wet_register.cpp").read_text()):
        name_to_addr[m.group(2)] = int(m.group(1), 16)
    ext = {}
    for f in sorted(gen_dir.glob("wet_recomp.*.cpp")):
        cur = None
        for line in f.open():
            m = FUNC.match(line)
            if m:
                cur = name_to_addr.get(m.group(1))
                if cur is not None:
                    ext[cur] = cur
                continue
            m = LABEL.match(line)
            if m and cur is not None:
                ext[cur] = max(ext[cur], int(m.group(1), 16))
    return ext


def main():
    gen_dir, cfg = Path(sys.argv[1]), Path(sys.argv[2])
    ext = extents(gen_dir)
    starts = sorted(ext)
    entries = [int(m.group(1), 16) for m in re.finditer(r"^0x([0-9A-F]{8}) =", cfg.read_text(), re.M)]
    for e in entries:
        hosts = [s for s in starts if s < e <= ext[s]]
        if hosts:
            print(f"0x{e:08X} inside 0x{hosts[-1]:08X} (labels up to 0x{ext[hosts[-1]]:08X})")


if __name__ == "__main__":
    main()
