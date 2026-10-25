"""Print the disassembly comments (and labels/calls) of a generated function.

Usage: python tools/analysis/show_func.py <generated/default> <sub_XXXXXXXX|address> [--code]
"""
import re
import sys
from pathlib import Path


def main():
    gen, name = Path(sys.argv[1]), sys.argv[2]
    code = "--code" in sys.argv
    if not name.startswith("sub_") and re.fullmatch(r"(0x)?[0-9A-Fa-f]{8}", name):
        name = "sub_" + name[-8:].upper()
    head = f"DEFINE_REX_FUNC({name})"
    for f in gen.glob("wet_recomp.*.cpp"):
        text = f.read_text()
        i = text.find(head)
        if i < 0:
            continue
        body = text[i: text.find("\n}\n", i) + 3]
        for line in body.splitlines():
            s = line.strip()
            if code or s.startswith("//") or s.startswith("loc_") or "__imp__" in s or re.match(r"sub_[0-9A-F]{8}\(", s) or s.startswith("DEFINE"):
                print(line.rstrip())
        return
    print("not found:", name)


if __name__ == "__main__":
    main()
