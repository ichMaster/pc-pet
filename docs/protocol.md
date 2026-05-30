# BLE protocol

Transport: Nordic UART Service (NUS). Device = peripheral / GATT server,
advertised as `PCpet`. PC = central, writes metric packets to RX.

## UUIDs

| Role | UUID |
|---|---|
| Service | `6E400001-B5A3-F393-E0A9-E50E24DCCA9E` |
| RX (PC -> device, write) | `6E400002-B5A3-F393-E0A9-E50E24DCCA9E` |
| TX (device -> PC, notify) | `6E400003-B5A3-F393-E0A9-E50E24DCCA9E` |

The device requests MTU 185; RX buffer is 200 bytes. Writes use Write Request
(`response=True`) for reliable delivery of the `onWrite` callback.

## Metric packet (PC -> device, RX)

A single ASCII line with three `;`-separated sections:

```
metrics ; cpuList ; ramList
```

### metrics section (comma-separated, fixed order)

| idx | field | range / notes |
|---|---|---|
| 0 | cpu | 0-100 (%) |
| 1 | ram | 0-100 (%) |
| 2 | temp | Celsius, -1 = unknown |
| 3 | net | KB/s, 0-9999 |
| 4 | procs | process count |
| 5 | topname | busiest process name (delimiters stripped, <=12 chars) |
| 6 | gpu | 0-100 (%), -1 = unknown |
| 7 | batt | host battery 0-100 (%), -1 = none |
| 8 | charging | 0 / 1 |
| 9 | diskR | disk read MB/s, 0-9999 |
| 10 | diskW | disk write MB/s, 0-9999 |

### cpuList / ramList sections

`name:val,name:val,...` — up to 4 entries each. `val` is a percent. Names are
cleaned of `, : ;` and truncated to ~10 chars.

### Example

```
37,72,61,540,889,Microsoft Wo,44,86,1,120,8;Microsoft:142,chrome:60,python:22,Window:9;chrome:18,Microsoft:12,Slack:9,Xcode:6
```

The two trailing metric values before the first `;` are `diskR` (120 MB/s) and
`diskW` (8 MB/s).

## Parsing on the device

The BLE write callback only copies raw bytes into a buffer and sets a flag
(callbacks run on the small-stack BT task). `loop()` drains the buffer and calls
`parsePacket()` on the main task, which splits by `;`, then the metrics by `,`,
then each proc list. Unknown / missing sections are treated as empty.

## Reverse channel (device -> PC, TX notify)

When the ENV III HAT is present and a central is connected, the device sends
environment telemetry over the TX characteristic (notify) roughly every 5
seconds (`ENV_SEND_MS = 5000`). The notify is skipped when the HAT is absent or
no central is connected.

### ENV telemetry line

A single ASCII line:

```
ENV;temp=%.1f;hum=%d;press=%d
```

| field | meaning | format |
|---|---|---|
| `ENV` | line marker / type discriminator | literal |
| `temp` | room temperature (SHT30) | float, 1 decimal, Celsius |
| `hum` | relative humidity (SHT30) | integer, percent |
| `press` | barometric pressure (QMP6988) | integer, hPa |

Example:

```
ENV;temp=22.5;hum=55;press=1013
```

The agent (`pc_pet_agent.py`) receives this on the TX characteristic, parses the
`ENV;`-prefixed line, and appends a row to a CSV log. The CSV header is:

```
timestamp,temp_c,humidity_pct,pressure_hpa
```

Example row:

```
2026-05-30T12:34:56,23.5,45,1013
```

Logging is controlled by agent flags: `--env-log` (path, default `env_log.csv`;
empty string disables), `--env-log-max-bytes` (default 5000000; the log rotates
when it reaches this size), and `--env-log-keep` (default 5; number of rotated
files to keep).

## Planned additions (see ROADMAP)

- **Config (PC -> device):** a line prefixed `CFG;` carrying thresholds, e.g.
  `CFG;hot=75;panic_cpu=85;panic_ram=92;panic_gpu=95;lowpwr=20;p1=10;p2=30`.
  The device distinguishes config from metrics by the first token.
- **Reverse channel commands (device -> PC, TX notify):** the TX channel is
  already used for ENV telemetry (documented above). Still planned: short
  command codes from a whitelist (lock, play/pause, volume, run script, kill
  top, kill selected).
