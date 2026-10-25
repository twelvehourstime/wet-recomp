"""Size candidate functions up to the next known function start (registered functions +
.pdata), trim trailing zero padding, and report branches leaving the range.

Usage: python tools/analysis/size_funcs.py <dump_dir> <known_funcs.txt> <addr> [...]
"""
import bisect
import sys

import tailcall_sizes as t


def sx(v, bits):
    return v - (1 << bits) if v & (1 << (bits - 1)) else v


def main():
    dump, known_path, addrs = sys.argv[1], sys.argv[2], [int(a, 16) for a in sys.argv[3:]]
    blocks = t.load_blocks(dump)
    known = sorted({int(x, 16) for x in open(known_path) if x.strip()} | set(t.pdata_starts(blocks)) | set(addrs))
    for a in addrs:
        end = nxt = known[bisect.bisect_right(known, a)]
        while t.word(blocks, end - 4) == 0:
            end -= 4
        leaving = []
        for pc in range(a, end, 4):
            w = t.word(blocks, pc)
            if w >> 26 == 18:
                tgt = (sx(w & 0x3FFFFFC, 26) + (0 if w & 2 else pc)) & 0xFFFFFFFF
            elif w >> 26 == 16:
                tgt = (sx(w & 0xFFFC, 16) + pc) & 0xFFFFFFFF
            else:
                continue
            if not (w & 1) and not a <= tgt < end:
                leaving.append(f"{pc:08X}->{tgt:08X}")
        print(f"0x{a:08X} size=0x{end - a:X} next=0x{nxt:08X} last={t.word(blocks, end - 4):08X} leaving={leaving}")


if __name__ == "__main__":
    main()
