"""Find guest float variables that track camera input across memtool dumps.

Usage: python tools/analysis/memdiff.py <memtool dir>
Expects dump-0..4: 0,1 still; 2,3 after equal right turns; 4 after a tilt.
Yaw candidates: 0==1, 1!=2, 2!=3 with same-sign steps of similar size, 3==4.
Pitch candidates: 0==1==2==3, 3!=4.
"""
import array
import math
import sys
from pathlib import Path

PAGE = 4096


def load_index(d, n):
    regions = []
    for line in open(d / f"dump-{n}.idx"):
        g, size, off = (int(x, 16) for x in line.split())
        regions.append((g, size, off))
    return regions


def main():
    d = Path(sys.argv[1])
    idxs = [load_index(d, i) for i in range(5)]
    files = [open(d / f"dump-{i}.bin", "rb") for i in range(5)]

    def page_offset(i, guest):
        # File offset of a guest page in dump i (regions differ between dumps).
        for g, size, off in idxs[i]:
            if g <= guest < g + size:
                return off + (guest - g)
        return None

    yaw, pitch = [], []
    for g, size, off in idxs[0]:
        if g >= 0xA0000000:  # physical mirrors of the same memory
            continue
        for p in range(0, size, PAGE):
            offs = [page_offset(i, g + p) for i in range(5)]
            if None in offs:
                continue
            pages = []
            for f, o in zip(files, offs):
                f.seek(o)
                pages.append(f.read(PAGE))
            if pages[1] == pages[2] and pages[3] == pages[4]:
                continue
            w = []
            for pg in pages:
                a = array.array("f")
                a.frombytes(pg[: len(pg) // 4 * 4])
                a.byteswap()
                w.append(a)
            for k in range(len(w[0])):
                v = [w[i][k] for i in range(5)]
                if any(math.isnan(x) or abs(x) > 1e4 for x in v):
                    continue
                addr = g + p + 4 * k
                if v[0] == v[1] and v[3] == v[4] and v[1] != v[2] and v[2] != v[3]:
                    s1, s2 = v[2] - v[1], v[3] - v[2]
                    if s1 * s2 > 0 and 0.3 < abs(s1 / s2) < 3:
                        yaw.append((addr, v))
                if v[0] == v[1] == v[2] == v[3] and v[3] != v[4]:
                    pitch.append((addr, v))
    print(f"yaw candidates: {len(yaw)}")
    for addr, v in yaw[:60]:
        print(f"  {addr:08X} " + " ".join(f"{x:+.5f}" for x in v))
    print(f"pitch candidates: {len(pitch)}")
    for addr, v in pitch[:60]:
        print(f"  {addr:08X} " + " ".join(f"{x:+.5f}" for x in v))


if __name__ == "__main__":
    main()
