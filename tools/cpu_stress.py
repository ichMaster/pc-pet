#!/usr/bin/env python3
"""
CPU stress generator for PC-Pet testing.

Drives the host CPU to a target load so you can watch the pet move through its
moods (sleep -> happy -> busy -> panic) without doing real work. Standard
library only, no external dependencies.

Examples:
    python cpu_stress.py                  # ~100% on all cores until Ctrl+C
    python cpu_stress.py --percent 70     # hold ~70% overall load
    python cpu_stress.py --percent 90 --duration 30
    python cpu_stress.py --ramp           # step 10 -> 40 -> 70 -> 95 and back
    python cpu_stress.py --percent 60 --workers 2

Mood reference (matches the firmware's currentMood):
    cpu < 15            -> SLEEP
    15..49             -> HAPPY
    cpu >= 50          -> BUSY
    cpu >= 85          -> PANIC   (also ram>=92 or gpu>=95)

Stop any run with Ctrl+C; all workers are terminated cleanly.
"""

import argparse
import math
import multiprocessing as mp
import os
import time


def _worker(duty, stop, window: float = 0.1):
    """Burn CPU for a `duty` fraction of each time `window`, sleep the rest.

    `duty` is a shared Value the parent can change on the fly (for --ramp).
    The inner math keeps the busy-spin from being optimized away.
    """
    x = 0.0
    while not stop.is_set():
        d = duty.value
        start = time.perf_counter()
        if d > 0:
            busy_until = start + window * min(d, 1.0)
            while time.perf_counter() < busy_until:
                x += math.sin(x) + math.cos(x)
        elapsed = time.perf_counter() - start
        rem = window - elapsed
        if rem > 0:
            time.sleep(rem)


def run(percent: int, workers: int, duration: float, ramp: bool, hold: float):
    ncores = os.cpu_count() or 1
    if workers is None or workers <= 0:
        workers = ncores

    stop = mp.Event()
    duty = mp.Value("d", 0.0)   # per-worker busy fraction (0..1), shared

    def set_target(p: int):
        # spread the requested overall percentage across the workers
        per = (p / 100.0) * ncores / workers
        duty.value = max(0.0, min(per, 1.0))

    procs = [mp.Process(target=_worker, args=(duty, stop), daemon=True)
             for _ in range(workers)]
    for pr in procs:
        pr.start()

    print(f"[stress] {workers} worker(s) on {ncores} core(s)")
    try:
        if ramp:
            levels = [10, 40, 70, 95, 70, 40, 10]
            for lvl in levels:
                set_target(lvl)
                print(f"[stress] target ~{lvl}% overall for {hold:.0f}s")
                time.sleep(hold)
        else:
            set_target(percent)
            tail = f" for {duration:.0f}s" if duration else " until Ctrl+C"
            print(f"[stress] target ~{percent}% overall{tail}")
            if duration:
                time.sleep(duration)
            else:
                while True:
                    time.sleep(1)
    except KeyboardInterrupt:
        print("\n[stress] interrupted")
    finally:
        print("[stress] stopping workers")
        stop.set()
        for pr in procs:
            pr.join(timeout=2)
        for pr in procs:
            if pr.is_alive():
                pr.terminate()


def main():
    ap = argparse.ArgumentParser(
        description="Generate CPU load to test PC-Pet moods.")
    ap.add_argument("--percent", type=int, default=100,
                    help="target overall CPU load %% (default 100)")
    ap.add_argument("--workers", type=int, default=None,
                    help="number of worker processes (default = CPU cores)")
    ap.add_argument("--duration", type=float, default=0,
                    help="seconds to run, 0 = until Ctrl+C (default 0)")
    ap.add_argument("--ramp", action="store_true",
                    help="step through 10/40/70/95%% and back to walk the moods")
    ap.add_argument("--hold", type=float, default=15,
                    help="seconds to hold each ramp level (default 15)")
    args = ap.parse_args()

    args.percent = max(0, min(args.percent, 100))
    run(args.percent, args.workers, args.duration, args.ramp, args.hold)


if __name__ == "__main__":
    main()
