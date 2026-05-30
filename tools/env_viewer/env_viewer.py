#!/usr/bin/env python3
"""
ENV CSV viewer TUI (PCP-011) -- a standalone terminal dashboard.

Reads the env_log.csv produced by the PC-Pet agent (PCP-009, rotated by
PCP-010) and charts room temperature / humidity / pressure over time. It is a
read-only consumer of the CSV: it never writes to or rotates the log, and does
not import from the agent or the firmware.

CSV schema (header row written by the agent):
    timestamp,temp_c,humidity_pct,pressure_hpa

Run:
    pip install -r requirements.txt
    python env_viewer.py --log ../../env_log.csv

Keys:
    q            quit
    tab / right  next metric        left   previous metric
    r            refresh now
    w            cycle time window (1h / 24h / all)
"""

import argparse
import csv
import os
from datetime import datetime

try:
    from textual.app import App, ComposeResult
    from textual.widgets import Header, Footer, Static
    from textual_plotext import PlotextPlot
except ImportError:  # pragma: no cover - dependency hint
    raise SystemExit(
        "Missing dependencies. Install them with:\n"
        "    pip install -r requirements.txt\n"
        "(textual + textual-plotext)"
    )


METRICS = [
    ("temp_c", "Temperature", "C"),
    ("humidity_pct", "Humidity", "%"),
    ("pressure_hpa", "Pressure", "hPa"),
]

WINDOWS = [("1h", 3600), ("24h", 86400), ("all", None)]


def parse_window(text):
    """Return the index into WINDOWS for a window label (default 24h)."""
    for i, (label, _) in enumerate(WINDOWS):
        if label == text:
            return i
    return 1  # 24h


def log_paths(base, include_rotated):
    """Active log first, then rotated .1 .. .N in chronological order.

    Rotated files are older than the active log, and .N is the oldest, so we
    read them in the order .N, .N-1, ..., .1, active to get chronological rows.
    """
    paths = []
    if include_rotated:
        i = 1
        rotated = []
        while os.path.exists(f"{base}.{i}"):
            rotated.append(f"{base}.{i}")
            i += 1
        paths.extend(reversed(rotated))  # oldest -> newest
    if os.path.exists(base):
        paths.append(base)
    return paths


def load_rows(base, include_rotated, window_secs):
    """Load (datetime, temp, hum, press) rows, filtered to the window.

    Tolerates a missing/empty file and skips malformed rows without raising.
    """
    rows = []
    for path in log_paths(base, include_rotated):
        try:
            with open(path, newline="", encoding="utf-8") as f:
                reader = csv.DictReader(f)
                for r in reader:
                    try:
                        ts = datetime.fromisoformat(r["timestamp"])
                        rows.append((
                            ts,
                            float(r["temp_c"]),
                            float(r["humidity_pct"]),
                            float(r["pressure_hpa"]),
                        ))
                    except (KeyError, ValueError, TypeError):
                        continue  # skip malformed row
        except OSError:
            continue  # file vanished mid-read (rotation); try next tick
    rows.sort(key=lambda x: x[0])
    if window_secs is not None and rows:
        cutoff = rows[-1][0].timestamp() - window_secs
        rows = [r for r in rows if r[0].timestamp() >= cutoff]
    return rows


class EnvViewer(App):
    BINDINGS = [
        ("q", "quit", "Quit"),
        ("tab", "next_metric", "Next metric"),
        ("right", "next_metric", "Next"),
        ("left", "prev_metric", "Prev"),
        ("r", "refresh", "Refresh"),
        ("w", "cycle_window", "Window"),
    ]

    def __init__(self, log_path, include_rotated, refresh, window):
        super().__init__()
        self._log = log_path
        self._rotated = include_rotated
        self._refresh = refresh
        self._metric = 0
        self._window = parse_window(window)

    def compose(self) -> ComposeResult:
        yield Header()
        yield PlotextPlot(id="chart")
        yield Static("", id="status")
        yield Footer()

    def on_mount(self):
        self.redraw()
        self.set_interval(self._refresh, self.redraw)

    def redraw(self):
        key, name, unit = METRICS[self._metric]
        win_label, win_secs = WINDOWS[self._window]
        rows = load_rows(self._log, self._rotated, win_secs)

        plot = self.query_one("#chart", PlotextPlot)
        plt = plot.plt
        plt.clear_data()
        plt.clear_figure()
        plt.title(f"{name} ({unit}) -- window {win_label}")

        col = {"temp_c": 1, "humidity_pct": 2, "pressure_hpa": 3}[key]
        ys = [r[col] for r in rows]
        if ys:
            plt.plot(list(range(len(ys))), ys)
        plot.refresh()

        status = self.query_one("#status", Static)
        if not rows:
            status.update(
                f"no data in {self._log!r} -- waiting for the agent to log ENV"
            )
            return
        span = rows[-1][0] - rows[0][0]
        latest = rows[-1]
        status.update(
            f"rows: {len(rows)}   span: {span}   "
            f"latest: {latest[1]:.1f}C  {int(latest[2])}%  {int(latest[3])}hPa   "
            f"[metric: {name}  window: {win_label}]"
        )

    def action_next_metric(self):
        self._metric = (self._metric + 1) % len(METRICS)
        self.redraw()

    def action_prev_metric(self):
        self._metric = (self._metric - 1) % len(METRICS)
        self.redraw()

    def action_refresh(self):
        self.redraw()

    def action_cycle_window(self):
        self._window = (self._window + 1) % len(WINDOWS)
        self.redraw()


def main():
    ap = argparse.ArgumentParser(description="PC-Pet ENV CSV viewer (TUI)")
    ap.add_argument("--log", default="env_log.csv", help="path to env_log.csv")
    ap.add_argument("--include-rotated", action="store_true",
                    help="also read rotated .1 .. .N files for full history")
    ap.add_argument("--refresh", type=float, default=5.0,
                    help="auto-refresh interval in seconds")
    ap.add_argument("--window", default="24h",
                    help="initial time window: 1h, 24h, or all")
    args = ap.parse_args()
    EnvViewer(args.log, args.include_rotated, args.refresh, args.window).run()


if __name__ == "__main__":
    main()
