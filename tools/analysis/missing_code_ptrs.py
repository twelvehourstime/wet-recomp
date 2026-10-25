"""Find function addresses that code builds with `lis rX,hi` + `addi/ori rY,rX,lo` and that
codegen did not emit as functions (callbacks, thread entry points, handler tables).

Usage: python tools/analysis/missing_code_ptrs.py <dump_dir> <known_funcs.txt>
Prints candidates with the first instructions, the instruction before, and the site that
builds the address, for manual review.
"""
import struct
import sys
from collections import defaultdict
from pathlib import Path

TEXT_START, TEXT_END = 0x82510000, 0x8338A550
MFLR_R12, BLR, BCTR, OR_R8 = 0x7D8802A6, 0x4E800020, 0x4E800420, 0x7D084378


def main():
    dump, known_path = Path(sys.argv[1]), sys.argv[2]
    known = {int(x, 16) for x in open(known_path) if x.strip()}
    text = (dump / f"image_{TEXT_START:x}.bin").read_bytes()
    n = len(text) // 4
    words = struct.unpack(f">{n}I", text[: n * 4])
    w = lambda a: words[(a - TEXT_START) // 4] if TEXT_START <= a < TEXT_START + n * 4 else None

    sites = defaultdict(list)
    for i, ins in enumerate(words):
        if ins >> 26 != 15 or (ins >> 16) & 31 != 0:  # lis rD,imm  (addis rD,0,imm)
            continue
        rd, hi = (ins >> 21) & 31, ins & 0xFFFF
        for j in range(i + 1, min(i + 8, n)):  # look a few instructions ahead
            nx = words[j]
            op, rs = nx >> 26, (nx >> 16) & 31
            if op == 14 and rs == rd:  # addi
                lo = nx & 0xFFFF
                val = ((hi << 16) + (lo - 0x10000 if lo & 0x8000 else lo)) & 0xFFFFFFFF
            elif op == 24 and ((nx >> 21) & 31) == rd:  # ori rA,rS,imm (rS field is bits 21-25)
                val = (hi << 16) | (nx & 0xFFFF)
            else:
                continue
            if TEXT_START <= val < TEXT_END and val % 4 == 0 and val not in known:
                sites[val].append(TEXT_START + i * 4)
            break

    for a in sorted(sites):
        first, prev = w(a), w(a - 4)
        if first in (None, 0) or prev == OR_R8:
            continue
        if not (first == MFLR_R12 or prev in (0, BLR, BCTR) or (prev is not None and prev >> 26 == 18 and not prev & 1)):
            continue
        print(f"0x{a:08X} first={first:08X} prev={prev:08X} built_at={','.join(f'{s:08X}' for s in sites[a][:3])}")


if __name__ == "__main__":
    main()
