"""Find code addresses referenced from data sections that codegen did not emit as functions.

Indirect calls (vtables, static-init tables, callbacks) only work if the target is a
generated function. This scans every aligned 32-bit word in non-code sections for values
pointing into .text that look like a function start, and reports the ones missing from
the generated function list.

Usage: python tools/analysis/missing_ptr_funcs.py <dump_dir> <known_funcs.txt>
  known_funcs.txt: one hex address per line (from DEFINE_REX_FUNC(sub_XXXXXXXX)).
"""
import struct
import sys
from collections import Counter
from pathlib import Path

TEXT = (0x82510000, 0x8338A550)
MFLR_R12 = 0x7D8802A6
BLR = 0x4E800020
BCTR = 0x4E800420


def main():
    dump, known_path = Path(sys.argv[1]), sys.argv[2]
    known = {int(x, 16) for x in open(known_path) if x.strip()}
    blocks = {int(f.stem.split("_")[1], 16): f.read_bytes() for f in dump.glob("image_*.bin")}
    text_start = 0x82510000
    text = blocks[text_start]

    def code_word(a):
        o = a - text_start
        return struct.unpack_from(">I", text, o)[0] if 0 <= o < len(text) - 3 else None

    def looks_like_start(a):
        w, prev = code_word(a), code_word(a - 4)
        if w is None or w == 0:
            return False
        return w == MFLR_R12 or prev in (0, BLR, BCTR) or (prev is not None and prev >> 26 == 18)

    refs = Counter()
    for start, data in blocks.items():
        if start in (text_start, 0x8338A600) or start >= 0x83580200:  # code, BINK code, .reloc
            continue
        for off in range(0, len(data) - 3, 4):
            v = struct.unpack_from(">I", data, off)[0]
            if TEXT[0] <= v < TEXT[1] and v % 4 == 0 and v not in known and looks_like_start(v):
                refs[v] += 1
    for a, n in sorted(refs.items()):
        w = code_word(a)
        print(f"0x{a:08X}  refs={n}  first={w:08X}{'  (mflr r12)' if w == MFLR_R12 else ''}")
    print(f"total: {len(refs)}", file=sys.stderr)


if __name__ == "__main__":
    main()
