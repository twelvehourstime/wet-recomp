"""Attribute long frames in a --perf_log_csv to likely causes.

Usage: python tools/analysis/stutter.py <csv> [--threshold-ms 20] [--show N]
For every frame over the threshold, shows the per-frame counters (pipeline/texture cache
misses, pipeline wait and shader translation time, command-buffer stalls, draw calls) and
summarizes how much of the long-frame time each measured cause explains.
"""
import argparse
import csv


def main():
    p = argparse.ArgumentParser()
    p.add_argument("csv")
    p.add_argument("--threshold-ms", type=float, default=20.0)
    p.add_argument("--show", type=int, default=25, help="print this many worst frames")
    a = p.parse_args()
    rows = [r for r in csv.DictReader(open(a.csv)) if r["frame_time_us"] not in ("", "0")]
    for r in rows:
        for k in r:
            r[k] = int(r[k] or 0)
    g = lambda r, k: r.get(k, 0)
    thr = a.threshold_ms * 1000
    long_ = [(i, r) for i, r in enumerate(rows) if r["frame_time_us"] > thr]
    normal = [r for r in rows if r["frame_time_us"] <= thr]
    print(f"frames={len(rows)} long(>{a.threshold_ms}ms)={len(long_)} ({100 * len(long_) / max(1, len(rows)):.2f}%)")
    keys = ["pipeline_cache_misses", "pipeline_wait_us", "shader_translate_us", "texture_cache_misses",
            "command_buffer_stalls"]
    for k in keys:
        on_long = sum(1 for _, r in long_ if g(r, k) > 0)
        on_norm = sum(1 for r in normal if g(r, k) > 0)
        print(f"  {k:24} non-zero on {on_long}/{len(long_)} long frames, {on_norm}/{len(normal)} normal frames")

    worst = sorted(long_, key=lambda x: -x[1]["frame_time_us"])[: a.show]
    print("\nworst frames: idx     ms  wait_ms xlate_ms pipe_miss tex_miss cb_stall draws  (prev pipe_miss)")
    for i, r in worst:
        prev = rows[i - 1]["pipeline_cache_misses"] if i > 0 else 0
        print(f"  {i:6d} {r['frame_time_us'] / 1000:7.1f} {g(r, 'pipeline_wait_us') / 1000:8.1f} "
              f"{g(r, 'shader_translate_us') / 1000:8.1f} {r['pipeline_cache_misses']:9d} "
              f"{r['texture_cache_misses']:8d} {r['command_buffer_stalls']:8d} {r['draw_calls']:6d}  ({prev})")

    tot = sum(r["frame_time_us"] for _, r in long_) or 1
    w = sum(g(r, "pipeline_wait_us") for _, r in long_)
    t = sum(g(r, "shader_translate_us") for _, r in long_)
    print(f"\nlong-frame time: {tot / 1000:.0f} ms; explained by pipeline waits {100 * w / tot:.0f}%, "
          f"shader translation {100 * t / tot:.0f}%")


if __name__ == "__main__":
    main()
