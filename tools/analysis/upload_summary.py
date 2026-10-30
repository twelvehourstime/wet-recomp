"""Summarize a perf CSV: frames binned by draw-call count (upload / CP cost per draw)."""
import csv, sys
rows = list(csv.DictReader(open(sys.argv[1])))
def f(r, k):
    try: return float(r[k])
    except (KeyError, ValueError, TypeError): return 0.0
bins = [(0, 300), (300, 800), (800, 1200), (1200, 1600), (1600, 2200), (2200, 9999)]
print(f"{'draws':>10} {'n':>5} {'ft_us':>7} {'fps':>5} {'cp_us':>7} {'up_MB':>6} {'ranges':>6} {'inval':>6} {'stalls':>6}")
for lo, hi in bins:
    seg = [r for r in rows if lo <= f(r, 'draw_calls') < hi]
    if not seg: continue
    g = lambda k: sum(f(r, k) for r in seg) / len(seg)
    print(f"{lo:>4}-{hi:<5} {len(seg):>5} {g('frame_time_us'):7.0f} {1e6/g('frame_time_us'):5.1f} {g('cp_thread_cpu_us'):7.0f} "
          f"{g('shared_mem_upload_bytes')/1048576:6.1f} {g('shared_mem_upload_ranges'):6.0f} {g('memory_invalidations'):6.0f} {g('command_buffer_stalls'):6.2f}")
