"""Minimal XDVDFS (Xbox / Xbox 360 disc) reader and extractor.

Usage:
    python tools/xdvdfs_extract.py list    <image.iso>
    python tools/xdvdfs_extract.py extract <image.iso> <out_dir>

Reads only; never modifies the image. Supports plain game-partition images and
full XGD1/XGD2/XGD3 dumps (the game partition is located by probing known offsets).
"""
import argparse
import os
import struct
import sys
from pathlib import Path

SECTOR = 2048
MAGIC = b"MICROSOFT*XBOX*MEDIA"
# Game-partition offsets: plain image, XGD3, XGD2, XGD1.
PARTITION_OFFSETS = (0x0, 0x2080000, 0xFD90000, 0x18300000)
ATTR_DIRECTORY = 0x10


class Image:
    def __init__(self, path):
        self.f = open(path, "rb")
        self.base = self._find_partition()
        vd = self._read(32 * SECTOR, 0x24)
        self.root_sector, self.root_size = struct.unpack_from("<II", vd, 0x14)

    def _read(self, offset, size, absolute=False):
        self.f.seek(offset if absolute else self.base + offset)
        return self.f.read(size)

    def _find_partition(self):
        for base in PARTITION_OFFSETS:
            head = self._read(base + 32 * SECTOR, 0x800, absolute=True)
            if head[:20] == MAGIC and head[0x7EC:0x800] == MAGIC:
                return base
        sys.exit("xdvdfs: no XDVDFS volume descriptor found")

    def entries(self, sector, size):
        """Yield (name, sector, size, attr) for one directory (binary-tree table)."""
        if size == 0:
            return
        table = self._read(sector * SECTOR, size)
        seen, stack = set(), [0]
        while stack:
            off = stack.pop()
            if off in seen or off + 14 > len(table):
                continue
            seen.add(off)
            left, right, sec, length, attr, nlen = struct.unpack_from("<HHIIBB", table, off)
            if left == 0xFFFF and right == 0xFFFF:
                continue
            name = table[off + 14: off + 14 + nlen].decode("latin-1")
            yield name, sec, length, attr
            for child in (left, right):
                if child not in (0, 0xFFFF):
                    stack.append(child * 4)

    def walk(self, sector=None, size=None, prefix=""):
        if sector is None:
            sector, size = self.root_sector, self.root_size
        for name, sec, length, attr in sorted(self.entries(sector, size)):
            path = f"{prefix}{name}"
            if attr & ATTR_DIRECTORY:
                yield path + "/", sec, length, True
                yield from self.walk(sec, length, path + "/")
            else:
                yield path, sec, length, False

    def copy_file(self, sector, length, dest):
        self.f.seek(self.base + sector * SECTOR)
        remaining = length
        with open(dest, "wb") as out:
            while remaining:
                chunk = self.f.read(min(remaining, 8 << 20))
                if not chunk:
                    raise IOError(f"short read extracting {dest}")
                out.write(chunk)
                remaining -= len(chunk)


def cmd_list(args):
    img = Image(args.image)
    print(f"partition offset 0x{img.base:X}, root sector {img.root_sector}, size {img.root_size}")
    total = 0
    for path, _sec, length, is_dir in img.walk():
        if not is_dir:
            total += length
        print(f"{'<dir>' if is_dir else length:>12}  {path}")
    print(f"total file bytes: {total}")


def cmd_extract(args):
    img = Image(args.image)
    out = Path(args.out_dir)
    out.mkdir(parents=True, exist_ok=True)
    count = 0
    for path, sec, length, is_dir in img.walk():
        dest = out / path
        if is_dir:
            dest.mkdir(parents=True, exist_ok=True)
            continue
        dest.parent.mkdir(parents=True, exist_ok=True)
        img.copy_file(sec, length, dest)
        count += 1
    print(f"extracted {count} files to {out}")


def main():
    p = argparse.ArgumentParser()
    sub = p.add_subparsers(dest="cmd", required=True)
    l = sub.add_parser("list")
    l.add_argument("image")
    l.set_defaults(func=cmd_list)
    e = sub.add_parser("extract")
    e.add_argument("image")
    e.add_argument("out_dir")
    e.set_defaults(func=cmd_extract)
    args = p.parse_args()
    args.func(args)


if __name__ == "__main__":
    main()
