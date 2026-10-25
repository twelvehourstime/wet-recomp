"""Summarize per-frame times from a --perf_log_csv file.

Usage: python tools/analysis/frametimes.py <csv> [--last N]
Prints mean/median/p95/p99/max frame time, effective fps, and a histogram of frames in
units of 60 Hz vblanks (1 = 16.7 ms, 2 = 33.3 ms, ...).
"""
import argparse
import csv
from collections import Counter


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("--last", type=int, default=0, help="only the last N frames")
    p.add_argument("--skip", type=int, default=0, help="skip the first N frames")
    a = p.parse_args()
    ft = [int(r["frame_time_us"]) for r in csv.DictReader(open(a.csv)) if r["frame_time_us"] not in ("", "0")]
    ft = ft[a.skip:]
    if a.last:
        ft = ft[-a.last:]
    s = sorted(ft)
    n = len(s)
    if not n:
        print("no frames")
        return
    mean = sum(s) / n
    print(f"frames={n} mean={mean / 1000:.2f}ms ({1e6 / mean:.1f} fps) median={s[n // 2] / 1000:.2f}ms "
          f"p95={s[int(n * .95)] / 1000:.2f}ms p99={s[int(n * .99)] / 1000:.2f}ms max={s[-1] / 1000:.2f}ms")
    hist = Counter(max(1, round(v / 16667)) for v in ft)
    print("vblanks/frame:", ", ".join(f"{k}:{v} ({100 * v / n:.1f}%)" for k, v in sorted(hist.items())))


if __name__ == "__main__":
    main()
