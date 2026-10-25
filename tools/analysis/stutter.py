"""Attribute long frames in a --perf_log_csv to likely causes.

Usage: python tools/analysis/stutter.py <csv> [--threshold-ms 20]
For every frame over the threshold, shows the per-frame counters (pipeline/texture cache
misses, command-buffer stalls, draw calls) and summarizes how many long frames coincide with
each, versus how often that counter is non-zero on normal frames.
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
    thr = a.threshold_ms * 1000
    long_ = [(i, r) for i, r in enumerate(rows) if r["frame_time_us"] > thr]
    normal = [r for r in rows if r["frame_time_us"] <= thr]
    print(f"frames={len(rows)} long(>{a.threshold_ms}ms)={len(long_)} ({100*len(long_)/max(1,len(rows)):.2f}%)")
    keys = ["pipeline_cache_misses", "pipeline_wait_us", "shader_translate_us", "texture_cache_misses",
            "command_buffer_stalls", "draw_calls"]
    for k in keys:
        on_long = sum(1 for _, r in long_ if r[k] > 0)
        on_norm = sum(1 for r in normal if r[k] > 0)
        print(f"  {k:24} non-zero on {on_long}/{len(long_)} long frames, {on_norm}/{len(normal)} normal frames")
    # The frame AFTER a pipeline miss is often the slow one (creation happens before the next present).
    after = sum(1 for i, _ in long_ if i > 0 and rows[i - 1]["pipeline_cache_misses"] > 0)
    print(f"  long frames whose previous frame had pipeline misses: {after}/{len(long_)}")
    worst = sorted(long_, key=lambda x: -x[1]["frame_time_us"])[: a.show]
    print("\nworst frames: idx  ms   pipe_miss tex_miss cb_stall draws  (prev frame pipe_miss)")
    for i, r in worst:
        prev = rows[i - 1]["pipeline_cache_misses"] if i > 0 else 0
        print(f"  {i:6d} {r['frame_time_us']/1000:7.1f} {r['pipeline_cache_misses']:9d} {r['texture_cache_misses']:8d} "
              f"{r['command_buffer_stalls']:8d} {r['draw_calls']:6d}  ({prev})")


if __name__ == "__main__":
    main()
