# ENV CSV viewer (TUI)

A standalone terminal dashboard (PCP-011) that charts the room temperature,
humidity, and pressure that the PC-Pet agent logs from the ENV III HAT
(`env_log.csv`, written by PCP-009 and rotated by PCP-010).

It is a **read-only consumer** of the CSV: it never writes to or rotates the
log, and does not import from the agent or the firmware. Its dependencies are
isolated here so the agent's runtime stays lean.

## Install

```bash
cd tools/env_viewer
python3 -m pip install -r requirements.txt   # textual + textual-plotext
```

## Run

```bash
python3 env_viewer.py --log ../../env_log.csv
```

If you run it from the directory where the agent writes `env_log.csv`, the
default `--log env_log.csv` is enough.

## Options

| Flag | Default | Meaning |
|------|---------|---------|
| `--log` | `env_log.csv` | path to the ENV CSV |
| `--include-rotated` | off | also read rotated `.1 .. .N` files for full history |
| `--refresh` | `5.0` | auto-refresh interval in seconds |
| `--window` | `24h` | initial time window: `1h`, `24h`, or `all` |

## Keys

| Key | Action |
|-----|--------|
| `q` | quit |
| `tab` / `right` | next metric (Temperature / Humidity / Pressure) |
| `left` | previous metric |
| `r` | refresh now |
| `w` | cycle time window (1h / 24h / all) |

The status bar shows the row count, the time span covered, and the latest value
of each metric. While the agent is running, the chart auto-refreshes as new rows
are appended. If the CSV is missing or empty, the viewer shows a "waiting for
data" state instead of crashing.

## CSV schema

```
timestamp,temp_c,humidity_pct,pressure_hpa
2026-05-30T14:03:21,23.4,45,1013
```
