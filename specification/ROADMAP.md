# PC-Pet — Roadmap (selected features)

Virtual pet on the M5StickC Plus2 whose mood mirrors the computer's state
(metrics arrive over BLE from a Python agent).

**Status:** packet delivery is fixed (`writes` increments, `RX(...)` lines flow);
core mechanics are live. This document is scoped to the features chosen for
implementation, after splitting #5 and #8 into smaller tasks.

---

## Selected features (after split)

| # | Feature | Notes |
|---|---|---|
| 1.1 | One-second tick | foundation for panic tiers |
| 2 | Panic tiers | 3 states by continuous-panic duration, instant revert |
| 6 | Disk | I/O only |
| 7 | ENV HAT | full — **ENV III confirmed (SHT30 + QMP6988)** |
| 8a | Buzzer patterns | device-only melody signatures (low complexity) |
| 8b | Threshold config channel | agent -> device `CFG;` line (higher complexity) |
| 5a | Reverse channel — safe commands | lock / play-pause / volume / run-script |
| 5b | Reverse channel — Kill | separate, low priority, **after #8** |

**Not selected:** 1.2 (NVS), 3 (achievements), 4 (touch/petting). Consequences in
"Dependency review".

---

## Dependency review (important)

1. **1.1 (tick)** is required by #2 — to measure panic duration via `millis()`
   rather than frames.
2. **NVS (1.2) NOT selected.** Consistent: #2 reverts instantly (nothing to
   persist), and the threshold config (8b) is re-sent by the agent on every
   connect. **Only consequence:** character and mute do not survive a reboot
   (always start as Blobby, sound on).
3. **8a (buzzer) is device-only** and self-contained — no protocol change. It is
   shared with #2 (panic tiers use the melody signatures), so build it before #2.
4. **8b (config channel)** introduces an agent -> device line, `CFG;`-prefixed,
   over the existing RX characteristic (no new characteristic). The device tells
   config from metrics by the first token (`CFG` vs a number). #2 can be built
   first with hardcoded panic-tier thresholds; 8b later makes them configurable.
5. **5a / 5b** both ride the existing TX (notify) characteristic. 5b (Kill) ties
   into the **Procs** screen for "kill selected" — Procs already exists. Kill is
   the only irreversible action, so it is isolated as its own low-priority task,
   scheduled after #8.
6. **#6, #7** are fully independent blocks. #7 is also autonomous from the PC.

---

## 1.1 One-second tick

**Goal:** a stable time base for panic tiers (frame rate varies: 55 ms active /
150 ms when the screen is off).

- A separate `tick1s` based on `millis()` updates duration counters once a second.
- **Effort:** low. Dependencies: none.

---

## 2. Panic tiers (replaces "health")

**Goal:** the longer the PC stays in panic, the "worse" the pet looks; the moment
panic clears, it returns to normal instantly.

### Logic
- `panicSeconds` counted by the tick (1.1) while `mood == M_PANIC` (optionally
  also `M_HOT` — see questions).
- Three tiers by duration:

| Tier | Continuous panic | Look | Sound (uses 8a) |
|---|---|---|---|
| PANIC-1 | 0 – T1 (default 10 s) | normal panic | short beep |
| PANIC-2 | T1 – T2 (default 30 s) | stronger distress | double beep |
| PANIC-3 | > T2 | critical / "sick" | insistent pattern |

- **Instant revert:** as soon as metrics drop below the panic threshold,
  `panicSeconds = 0` and the mood immediately reflects current metrics. No slow
  recovery, no persistence.
- T1/T2 are hardcoded defaults until 8b makes them configurable.

### Open questions
- Defaults for T1/T2 (10 s / 30 s?).
- Count only `M_PANIC`, or include `M_HOT` in the tier timer.

**Effort:** low-medium. Dependencies: 1.1; sound from 8a.

---

## 6. Disk — I/O only

**Goal:** add disk activity to the PC picture.

### Agent
- `psutil.disk_io_counters()` -> delta of `read_bytes` / `write_bytes` per
  interval -> read/write throughput.
- Add two fields to the metric packet (read, write).

### Device
- Show in Stats as a row (R / W). No graphs (per the choice — I/O only).

### Open questions
- Units: MB/s or KB/s (recommendation: MB/s with one decimal, since SSDs hit tens
  to hundreds of MB/s).

**Effort:** low. Dependencies: none (packet grows ~8-10 bytes, fits buffer 200 and MTU).

---

## 7. ENV HAT — environment monitoring (full) — ENV III

**Goal:** give the stick its own senses — room temperature, humidity, pressure;
a dedicated screen plus a mood influence. Works even without a BLE link to the PC
(autonomous).

### Hardware — confirmed ENV III
- Sensors: **SHT30** (temperature + humidity) + **QMP6988** (barometric pressure).
- Library: **M5Unit-ENV** (use the ENV III / QMP6988 path, not BMP280).
- HAT on pins **G0 (SDA)** / **G26 (SCL)** of the top 8-pin connector.
- Initialize on a separate bus: `Wire1.begin(0, 26)` so it does not clash with the
  internal IMU/RTC on the main bus. (`Wire.begin(0,26)` is otherwise mandatory for
  the HAT, since I2C defaults to the Grove port.)
- **Caveat:** G0 is a strap pin; some I2C sensors on (0,26) can interfere with
  boot. Initialize with a presence check (skip ENV cleanly if the HAT is absent).
- I2C addresses: SHT30 at 0x44, QMP6988 at 0x70 — no conflict.

### Full scope
- **ENV screen:** room temperature, humidity %, pressure hPa + mini trend.
- **Mood influence:** hot/stuffy room -> a "stuffy" modifier; sharp pressure drop
  -> "weather turning".
- **Pressure log** for a simple barometer (trend up/down).
- **Option:** send ENV data back to the agent (log the workplace climate).

### Open questions
- Whether to send ENV back to the PC.

**Effort:** low-medium. Dependencies: none (hardware only). Autonomous from the PC.

---

## 8a. Buzzer patterns (device-only) — low complexity

**Goal:** give each event its own sound. Self-contained, no protocol change.

- A "play melody" helper (array of frequency/duration pairs).
- A signature per event: PANIC-1 / PANIC-2 / PANIC-3 (used by #2), low-power,
  stick low-battery.
- Mute (double click BtnB) is respected.

### Open questions
- Mute global (as now) or per-type.

**Effort:** low. Dependencies: none. Shared with #2 — build before/with it.

---

## 8b. Threshold config channel (agent -> device) — higher complexity

**Goal:** edit alert thresholds in one place (the agent) instead of in firmware.

### Config channel
- The agent sends the device a prefixed line over the existing RX characteristic,
  e.g. `CFG;hot=75;panic_cpu=85;panic_ram=92;panic_gpu=95;lowpwr=20;p1=10;p2=30`.
- The device distinguishes config from metrics by the first token (`CFG` vs a number).
- Sent on connect + on change. No NVS needed — the agent always re-sends.

### Configurable values
- Mood thresholds (`hot`, `panic_cpu/ram/gpu`, `lowpwr`).
- Panic tier thresholds (`p1`, `p2`) for #2.

**Why higher complexity:** cross-cutting (agent + device), changes the RX parse
path (branch on first token), and must not break the existing metric parsing.

**Effort:** low-medium. Dependencies: parse path in firmware; agent send-on-connect.

---

## 5a. Reverse channel (PC <- device) — safe commands

**Goal:** the stick as a Mac remote for non-destructive actions. Technically
ready — the TX (notify) characteristic already exists.

### Scheme
Device sends a command code over TX -> agent subscribed (`start_notify` in bleak)
-> executes from a **whitelist**.

### Command set (safe only)
| Command | macOS implementation | Risk |
|---|---|---|
| Lock screen | `pmset displaysleepnow` | safe |
| Play / Pause | AppleScript media key | safe |
| Volume +/- | AppleScript | safe |
| Run script | shell (fixed path) | low |

### On-device UX
- A new **Remote** screen (added to the BtnA cycle) listing commands; BtnA cycles,
  long-press BtnA executes.

### Safety
- Whitelist only (fixed set), never "run arbitrary input from the stick".

### Open questions
- User script path (one slot or several).

**Effort:** medium (two-button UX). Dependencies: TX characteristic (exists).

---

## 5b. Reverse channel — Kill (separate, low priority, after #8)

**Goal:** kill processes from the stick. Isolated because it is the only
**irreversible** action; deferred to low priority and scheduled after #8.

### Command set (destructive)
| Command | macOS implementation | Risk |
|---|---|---|
| Kill heaviest | psutil kill | irreversible |
| Kill selected (from Procs) | psutil kill by name | irreversible |

### On-device UX
- Kill requires on-screen confirmation: "Kill chrome? B=yes A=no".
- Process selection for "kill selected" happens on the Procs screen.

### Safety
- Irreversible actions only with explicit confirmation, always.

### Open questions
- Exact kill confirmation flow.

**Effort:** medium-high (confirmation UX + safety). Dependencies: 5a (remote
plumbing), Procs screen (exists). Scheduled last.

---

## Implementation phases

### Phase 1 — Core mechanics (stages 1-4)

Foundation + first real gameplay features. All device-side except disk I/O.

| Stage | Content | Effort |
|---|---|---|
| 1 | 1.1 tick — 1 Hz `millis()`-based counter | low |
| 2 | 8a buzzer patterns — per-event melody signatures | low |
| 3 | #2 panic tiers — 3 escalating states by duration | low-medium |
| 4 | #6 disk I/O — read/write MB/s in agent + Stats row | low |

Rationale: 1.1 + 8a are the cheap foundation; #2 lands on them with hardcoded
thresholds. #6 is small and independent, rounds out the metric picture.

### Phase 2 — Environment sensing (stage 5)

| Stage | Content | Effort |
|---|---|---|
| 5 | #7 ENV III — SHT30 + QMP6988 on Wire1(0,26) | low-medium |

Autonomous from the PC link. Adds an ENV screen (temp / humidity / pressure)
and an optional mood modifier. Demo-friendly; works even without BLE.

### Phase 3 — PlatformIO migration + configurable thresholds + security review (stage 6)

| Stage | Content | Effort |
|---|---|---|
| 6-pre | Migrate firmware build from Arduino IDE to PlatformIO | low-medium |
| 6a | 8b threshold config channel — `CFG;`-prefixed line over RX | low-medium |
| 6b | Security review of BLE input parsing | low |

**Build migration (6-pre):** starting from this phase the firmware builds with
PlatformIO instead of the Arduino IDE. Steps:
- Add `platformio.ini` at the repo root targeting `m5stick-c` (ESP32-PICO,
  arduino framework).
- Declare library dependencies: `m5stack/M5Unified`, `m5stack/M5GFX`.
- Move the sketch from `firmware/pc_tamagotchi/pc_tamagotchi.ino` to
  `firmware/src/main.cpp` (rename + add `#include <Arduino.h>`).
- Verify the build compiles and uploads identically to the Arduino IDE build.
- Update README build instructions for both PlatformIO CLI (`pio run -t upload`)
  and the PlatformIO VS Code extension.
- Keep the old `.ino` path noted in a one-time migration note so anyone on the
  Arduino IDE path knows where the source moved.

Phases 1-2 remain Arduino IDE. Phases 3-5 use PlatformIO.

Cross-cutting (agent + device). Makes mood and panic-tier thresholds
configurable from the agent side. Sent on connect; no NVS needed.

Security review scope: the config channel introduces a second parse path on the
device for external input. Review covers bounds-checking of threshold values,
buffer-overflow safety in the `CFG;` parser, rejection of malformed lines, and
ensuring the metric parse path cannot be tricked by a `CFG`-shaped metric packet.
Also audit the existing `parsePacket()` path (atoi overflow, strncpy bounds,
strtok edge cases) while the parsing code is open.

### Phase 4 — Remote control (stage 7)

| Stage | Content | Effort |
|---|---|---|
| 7 | 5a reverse channel — safe commands over TX notify | medium |

Lock screen, play/pause, volume, run-script. A new Remote screen in
the BtnA cycle; long-press to execute. Whitelist only.

### Phase 5 — Kill (stage 8)

| Stage | Content | Effort |
|---|---|---|
| 8 | 5b Kill — kill top / kill selected from Procs | medium-high |

The only irreversible action. On-screen confirmation required. Depends on 5a
plumbing. Scheduled last per its risk and low priority.

---

## Open questions summary

1. **#2:** defaults for T1/T2 (10 s / 30 s?); count only `M_PANIC` or also `M_HOT`.
2. **#6:** I/O units — MB/s or KB/s.
3. **#7:** whether to send ENV data back to the PC. (Version resolved: ENV III.)
4. **8a:** mute global (as now) or per-type.
5. **5a:** user script path(s) — one slot or several.
6. **5b:** exact kill confirmation flow.
