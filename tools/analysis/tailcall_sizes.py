"""Size candidate functions that codegen reports as 'target not in any function'.

For each address, the function is assumed to run up to the next .pdata function
start (or next given address), minus trailing zero padding words. Prints TOML
[functions] entries for review.

Usage: python tools/analysis/tailcall_sizes.py <dump_dir> <addr> [<addr> ...]
  dump_dir holds image_<start>.bin files written by tools/ghidra/DumpAround.java.
"""
import struct
import sys
from pathlib import Path


def load_blocks(dump):
    blocks = []
    for f in Path(dump).glob("image_*.bin"):
        blocks.append((int(f.stem.split("_")[1], 16), f.read_bytes()))
    return blocks


def word(blocks, addr):
    for start, data in blocks:
        if start <= addr < start + len(data) - 3:
            return struct.unpack_from(">I", data, addr - start)[0]
    raise KeyError(hex(addr))


def pdata_starts(blocks):
    start, data = next(b for b in blocks if b[0] == 0x824A7200)
    starts = set()
    for off in range(0, len(data) - 7, 8):
        begin, info = struct.unpack_from(">II", data, off)
        if begin:
            starts.add(begin)
    return sorted(starts)


def main():
    dump, addrs = sys.argv[1], sorted({int(a, 16) for a in sys.argv[2:]})
    blocks = load_blocks(dump)
    bounds = sorted(set(pdata_starts(blocks)) | set(addrs))
    for a in addrs:
        nxt = next(b for b in bounds if b > a)
        end = nxt
        while end > a and word(blocks, end - 4) == 0:
            end -= 4
        last = word(blocks, end - 4)
        kind = {0x4E800020: "blr"}.get(last, "b" if last >> 26 == 18 else f"{last:08X}")
        print(f'[functions.0x{a:08X}]  # size 0x{end - a:X}, ends with {kind}, next fn 0x{nxt:08X}')
        print(f"size = 0x{end - a:X}")


if __name__ == "__main__":
    main()
