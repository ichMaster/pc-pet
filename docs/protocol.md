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

### cpuList / ramList sections

`name:val,name:val,...` — up to 4 entries each. `val` is a percent. Names are
cleaned of `, : ;` and truncated to ~10 chars.

### Example

```
37,72,61,540,889,Microsoft Wo,44,86,1;Microsoft:142,chrome:60,python:22,Window:9;chrome:18,Microsoft:12,Slack:9,Xcode:6
```

## Parsing on the device

The BLE write callback only copies raw bytes into a buffer and sets a flag
(callbacks run on the small-stack BT task). `loop()` drains the buffer and calls
`parsePacket()` on the main task, which splits by `;`, then the metrics by `,`,
then each proc list. Unknown / missing sections are treated as empty.

## Planned additions (see ROADMAP)

- **Config (PC -> device):** a line prefixed `CFG;` carrying thresholds, e.g.
  `CFG;hot=75;panic_cpu=85;panic_ram=92;panic_gpu=95;lowpwr=20;p1=10;p2=30`.
  The device distinguishes config from metrics by the first token.
- **Disk I/O:** two extra metric fields (read, write MB/s).
- **Reverse channel (device -> PC, TX notify):** short command codes from a
  whitelist (lock, play/pause, volume, run script, kill top, kill selected).
