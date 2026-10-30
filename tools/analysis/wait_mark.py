"""Wait until logs/marks.txt gains a new line (F2 in game); print the new mark lines.

Usage: python tools/analysis/wait_mark.py [--timeout 600] [--marks logs/marks.txt]
"""
import argparse, os, time

p = argparse.ArgumentParser()
p.add_argument("--timeout", type=float, default=600)
p.add_argument("--marks", default=os.path.join(os.path.dirname(__file__), "..", "..", "logs", "marks.txt"))
a = p.parse_args()
path = os.path.abspath(a.marks)

def read():
    try:
        with open(path) as f:
            return f.read().splitlines()
    except FileNotFoundError:
        return []

before = read()
deadline = time.time() + a.timeout
while time.time() < deadline:
    now = read()
    if len(now) > len(before):
        print("\n".join(now[len(before):]))
        raise SystemExit(0)
    time.sleep(0.5)
print("timeout")
raise SystemExit(1)
