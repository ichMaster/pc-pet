# Phase 2 -- Environment sensing

ENV III HAT (SHT30 + QMP6988) on the M5StickC Plus2. The stick reads its own
room temperature, humidity, and pressure, shows them on a dedicated screen,
lets them influence the pet's mood, and (optionally) reports them back to the PC
agent. The sensor screen works even without a BLE link (autonomous); the
telemetry path adds an agent + protocol change.

Scope decisions (from ROADMAP open questions):
- **ENV-to-PC telemetry: included** (PCP-009) -- device sends ENV over the TX
  notify characteristic; the agent reviews it via an append-only `env_log.csv`
  (timestamped) plus a one-line console echo. No GUI/plot tool in scope.
- **ENV mood modifier: included** (PCP-008) -- stuffy-room and weather-turning
  influences.

## Issues Summary Table

| # | ID | Title | Size | Stage | Dependencies |
|---|---|---|---|---|---|
| 1 | PCP-005 | ENV III sensor bring-up | M | 1 -- Sensors | -- |
| 2 | PCP-006 | ENV screen | M | 2 -- Display | PCP-005 |
| 3 | PCP-007 | Pressure trend log | S | 3 -- Trend | PCP-005, PCP-006 |
| 4 | PCP-008 | ENV mood modifier | M | 4 -- Mood | PCP-005, PCP-007 |
| 5 | PCP-009 | ENV telemetry to PC | M | 5 -- Telemetry | PCP-005 |
| 6 | PCP-010 | ENV log retention (rotation) | S | 5 -- Telemetry | PCP-009 |
| 7 | PCP-011 | ENV CSV viewer TUI (separate app) | M | 6 -- Tooling | PCP-009 |

**Size legend:** S = 1-2 days, M = 3-5 days, L = 5-8 days

---

## Dependency Tree

```
                 PCP-005 (sensor bring-up)
                     |
        +------------+------------+------------------+
        |            |            |                  |
    PCP-006      (PCP-007      PCP-009            (PCP-008 also
    (ENV screen)  needs 006)   (telemetry)        needs 007)
        |            |            |
        +-----+------+        PCP-009 (telemetry, writes env_log.csv)
              |              /            \
          PCP-007           PCP-010        PCP-011
          (trend)           (retention)    (viewer TUI, separate app)
              |
          PCP-008 (mood modifier)
```

**Parallelization hints:**

- PCP-005 must land first -- it provides the sensor reads everything else uses.
- PCP-009 (telemetry) is independent of the screen/trend/mood chain -- it only
  needs PCP-005, so it can run in parallel with PCP-006/007/008.
- PCP-010 (retention) and PCP-011 (viewer) both consume the `env_log.csv` from
  PCP-009 and are independent of each other -- they can run in parallel once
  PCP-009 lands. PCP-011 is a fully separate application (own dependencies).
- PCP-006 -> PCP-007 -> PCP-008 form a sequential chain (screen, then trend on
  the screen, then mood modifier that reuses the trend).

---

## Stage 1 -- Sensors

### PCP-005 -- ENV III sensor bring-up (SHT30 + QMP6988 on Wire1)

**Description:**
Bring up the ENV III HAT over a dedicated I2C bus and expose its readings as
globals. This is the foundation for the ENV screen, the pressure trend, the
mood modifier, and the telemetry. The HAT carries an SHT30 (temperature +
humidity, 0x44) and a QMP6988 (barometric pressure, 0x70). Autonomous from the
PC -- no BLE involvement in this issue.

**What needs to be done:**
- Add the **M5Unit-ENV** library dependency (ENV III / QMP6988 path, NOT BMP280).
  Document it in the README "Install libraries" section.
- Initialize the sensors on a **separate I2C bus** so they do not clash with the
  internal IMU/RTC on the main bus:
  ```cpp
  #include "M5UnitENV.h"
  SHT3X    g_sht30;
  QMP6988  g_qmp6988;
  ```
  Init in `setup()` on `Wire1` with the HAT pins (G0 = SDA, G26 = SCL):
  ```cpp
  bool sht_ok = g_sht30.begin(&Wire1, SHT3X_I2C_ADDR, 0, 26, 400000U);
  bool qmp_ok = g_qmp6988.begin(&Wire1, QMP6988_SLAVE_ADDRESS_L, 0, 26, 400000U);
  g_envPresent = sht_ok && qmp_ok;
  ```
- **Presence check is mandatory.** G0 is a strap pin; a sensor on (0,26) can
  interfere with boot. If `begin()` fails for either sensor, set
  `g_envPresent = false` and skip all ENV reads cleanly (no crash, no hang).
- Declare globals above `setup()` (per project convention):
  ```cpp
  bool  g_envPresent = false;
  float g_envTemp    = 0;    // room temperature, Celsius
  float g_envHum     = 0;    // relative humidity, %
  float g_envPress   = 0;    // barometric pressure, hPa
  uint32_t g_lastEnvRead = 0;
  ```
- Read the sensors on a slow cadence (~2 s) from a `millis()`-gated block in
  `loop()` -- the ENV values change slowly and the I2C reads are blocking, so do
  not read every frame:
  ```cpp
  if (g_envPresent && millis() - g_lastEnvRead >= 2000) {
    g_lastEnvRead = millis();
    if (g_sht30.update())   { g_envTemp = g_sht30.cTemp; g_envHum = g_sht30.humidity; }
    if (g_qmp6988.update()) { g_envPress = g_qmp6988.pressure / 100.0f; }  // Pa -> hPa
  }
  ```
- Add a one-line boot message reporting whether the HAT was detected
  (`Serial.printf("ENV III: %s\n", g_envPresent ? "present" : "absent")`).
- Do NOT use the main `Wire` bus (it carries the IMU/RTC).
- Do NOT block boot if the HAT is absent.
- Do NOT add an ENV screen, mood modifier, or telemetry here -- bring-up only.

**Dependencies:** None

**Expected result:**
On boot, the firmware detects the ENV III HAT (or cleanly reports its absence),
and `g_envTemp`, `g_envHum`, `g_envPress` hold live readings updated every 2 s.
Without the HAT, the device runs exactly as before.

**Acceptance criteria:**
- [ ] M5Unit-ENV library added and documented in README
- [ ] SHT30 (0x44) and QMP6988 (0x70) initialized on `Wire1.begin(0, 26)`
- [ ] Boot serial line reports ENV present/absent
- [ ] With the HAT attached, `g_envTemp` / `g_envHum` / `g_envPress` show plausible live values (temp ~20-28 C, humidity ~30-60%, pressure ~980-1030 hPa)
- [ ] Without the HAT, the device boots and runs normally (presence check skips ENV)
- [ ] ENV reads are gated (~2 s), not every frame
- [ ] Only `firmware/pc_tamagotchi/pc_tamagotchi.ino` and `README.md` changed

---

## Stage 2 -- Display

### PCP-006 -- ENV screen (temp / humidity / pressure)

**Description:**
Add a dedicated ENV screen to the BtnA view cycle showing the room temperature,
humidity, and pressure from PCP-005. Shown only when the HAT is present; if
absent, skipped in the cycle.

**What needs to be done:**
- Add `VIEW_ENV` to the `View` enum. Recommended placement: after Procs
  (Pet -> Stats -> Graph -> Procs -> ENV).
- When `g_envPresent` is false, skip `VIEW_ENV` in the BtnA cycle so the cycle
  stays clean on units without the HAT.
- Implement `viewEnv()`:
  - Reuse `renderTopBar(...)` for the header (BLE dot, character/mute, battery).
  - Title "ENV" (size 2).
  - Three labeled rows with values:
    - `temp: 23.4 C`
    - `humid: 45 %`
    - `press: 1013 hPa`
  - One decimal for temperature, integer for humidity and pressure.
  - Optional color accent per row (warm color for temp, blue for humidity).
- Wire `VIEW_ENV` into the render dispatch in `loop()`:
  ```cpp
  case VIEW_ENV: viewEnv(connected); break;
  ```
- Keep rendering through the offscreen `M5Canvas` then `pushSprite`.
- Do NOT add the pressure trend graph here (that is PCP-007) -- current reading
  only.

**Dependencies:** PCP-005

**Expected result:**
Cycling with BtnA reaches an ENV screen showing live room temperature, humidity,
and pressure. On a unit without the HAT, the ENV screen is skipped.

**Acceptance criteria:**
- [ ] `VIEW_ENV` added to the view enum and the BtnA cycle
- [ ] ENV screen shows temp (1 decimal), humidity (%), pressure (hPa) with live values
- [ ] Values update at the ~2 s ENV read cadence
- [ ] Without the HAT, ENV screen is skipped
- [ ] Top bar renders consistently with the other screens
- [ ] Rendering goes through `M5Canvas` + `pushSprite`
- [ ] Only `firmware/pc_tamagotchi/pc_tamagotchi.ino` changed

---

## Stage 3 -- Trend

### PCP-007 -- Pressure trend log (barometer arrow + mini graph)

**Description:**
Log barometric pressure over time so the ENV screen can show whether pressure is
rising or falling -- a simple barometer / "weather turning" indicator. The
classified trend is also reused by the mood modifier (PCP-008).

**What needs to be done:**
- Add a pressure history ring buffer (sampled slowly, e.g. once per minute):
  ```cpp
  const int PRESS_HIST = 60;          // 60 samples
  float    g_pressHist[PRESS_HIST];
  int      g_pressHistN = 0;          // number of valid samples
  int      g_pressHistPos = 0;
  uint32_t g_lastPressLog = 0;
  const uint32_t PRESS_LOG_MS = 60000;  // log once per minute
  ```
- In a `millis()`-gated block, append the current `g_envPress` to the ring
  buffer every `PRESS_LOG_MS`.
- Compute a trend over the recent window (delta between oldest valid sample and
  newest). Classify into a small enum/int that PCP-008 can reuse:
  - rising  (delta > +0.5 hPa over the window)
  - falling (delta < -0.5 hPa)
  - steady  (otherwise)
  Expose it, e.g. `int pressTrend()` returning -1 / 0 / +1.
- On the ENV screen (PCP-006), show a trend indicator next to the pressure row:
  an arrow (up / down / flat) or a word (rising / falling / steady).
- Optional: a small sparkline of `g_pressHist` under the pressure row (reuse the
  graph-drawing approach from `viewGraph`, scaled to the pressure range).
- Before enough samples exist, show "--" / no arrow (no false trend).
- Do NOT persist the history across reboots (no NVS in scope).

**Dependencies:** PCP-005, PCP-006

**Expected result:**
The ENV screen shows whether barometric pressure is rising, falling, or steady,
based on a logged history, via a reusable `pressTrend()` helper.

**Acceptance criteria:**
- [ ] Pressure logged to a ring buffer on a slow cadence (~1/min)
- [ ] `pressTrend()` returns rising / falling / steady over the window
- [ ] ENV screen shows a trend arrow or word next to the pressure value
- [ ] Before enough samples exist, shows a neutral placeholder (no false trend)
- [ ] (Optional) pressure sparkline renders on the ENV screen
- [ ] History is in-memory only (no NVS), rebuilds after reboot
- [ ] Only `firmware/pc_tamagotchi/pc_tamagotchi.ino` changed

---

## Stage 4 -- Mood

### PCP-008 -- ENV mood modifier (stuffy room / weather turning)

**Description:**
Let the room environment influence the pet. A hot, humid room makes the pet
"stuffy"; a sharp pressure drop makes it sense "weather turning." This is a
modifier layered on top of the existing PC-driven mood, not a replacement -- PC
alert states (PANIC, HOT) still take priority.

**What needs to be done:**
- Define ENV modifier thresholds as constants (hardcoded for now; a future 8b
  config channel could expose them):
  ```cpp
  const float ENV_STUFFY_TEMP = 27.0f;   // room temp C
  const float ENV_STUFFY_HUM  = 60.0f;   // humidity %
  // weather-turning uses the pressTrend() falling signal from PCP-007
  ```
- Add a helper that returns an ENV modifier state, e.g.:
  ```cpp
  enum EnvMod { ENV_NONE, ENV_STUFFY, ENV_WEATHER };
  EnvMod envModifier();   // STUFFY if hot+humid, WEATHER if pressure falling sharply
  ```
- **Compose with the existing mood without breaking priority.** The PC-driven
  `currentMood()` result stays authoritative for alert states. The ENV modifier
  only applies when the PC mood is a calm state (e.g. M_HAPPY or M_SLEEP):
  - Decide the exact composition rule and document it in a comment. Recommended:
    if `g_envPresent` and PC mood is M_HAPPY, a STUFFY room nudges the displayed
    state toward M_STUFFED; a WEATHER-turning signal shows a small indicator.
  - Do NOT let an ENV modifier override M_PANIC / M_HOT / M_LOWPWR.
- Surface the modifier subtly:
  - A small badge/word on the Pet screen ("stuffy" / "weather") and/or
  - A minor expression tweak -- keep it light; do not add a whole new mood face.
- Only active when `g_envPresent` is true.
- Do NOT make the thresholds configurable yet (deferred to 8b).
- Do NOT couple this to the PC link -- it works offline.

**Dependencies:** PCP-005, PCP-007

**Expected result:**
When the room is hot and humid (or pressure is dropping sharply) and the PC is
not in an alert state, the pet reflects a "stuffy" or "weather turning" mood
influence. PC alert states always take priority.

**Acceptance criteria:**
- [ ] `envModifier()` returns NONE / STUFFY / WEATHER from ENV readings + trend
- [ ] STUFFY triggers on high temp + high humidity; WEATHER on sharp pressure fall
- [ ] Modifier never overrides M_PANIC / M_HOT / M_LOWPWR (priority preserved)
- [ ] Modifier only applies when `g_envPresent` is true
- [ ] A subtle on-screen indication of the active modifier
- [ ] Composition rule documented in a code comment
- [ ] Only `firmware/pc_tamagotchi/pc_tamagotchi.ino` changed

---

## Stage 5 -- Telemetry

### PCP-009 -- ENV telemetry to PC (device -> agent over TX notify)

**Description:**
Send the ENV readings back to the PC agent so the workplace climate can be
logged. The device already exposes a TX (notify) characteristic; this issue
uses it for the first time (device -> PC direction). The agent subscribes and
logs the values. This is the first reverse-channel use and is additive -- it
must not affect the existing PC -> device metric path.

**What needs to be done:**
- **Protocol** (`docs/protocol.md`): document a device -> PC notify line for ENV,
  prefixed so the agent can distinguish it from any future reverse-channel
  message. Recommended format:
  ```
  ENV;temp=23.4;hum=45;press=1013
  ```
  Document it under a new "Reverse channel (device -> PC, TX notify)" section.
- **Firmware:**
  - On a slow cadence (e.g. every 5-10 s, gated by `millis()`), if connected and
    `g_envPresent`, build the `ENV;...` line and send it via the TX
    characteristic `notify()`.
  - Keep the send small (well under MTU). Do not send if not connected or no HAT.
  - Reuse the existing TX characteristic; do NOT add a new characteristic.
- **Agent** (`agent/pc_pet_agent.py`):
  - Subscribe to the TX characteristic with `start_notify`.
  - Parse `ENV;...` lines; ignore anything that is not an ENV line (forward-compat
    for future reverse-channel commands).
  - **Review mechanism: append-only CSV + console echo** (decided).
    - Append a timestamped row to `env_log.csv` for each reading, with a header
      written once if the file is new:
      ```
      timestamp,temp_c,humidity_pct,pressure_hpa
      2026-05-30T14:03:21,23.4,45,1013
      ```
      Use an ISO-8601 local timestamp. The CSV is the durable artifact the user
      reviews -- open it in Numbers/Excel or feed it to a plotting script.
    - Echo each reading to the agent console as a one-line summary, e.g.
      `[env] 23.4C  45%  1013hPa`, so the link is visibly working at a glance.
    - Make the CSV path configurable via a CLI flag (e.g. `--env-log env_log.csv`,
      default `env_log.csv` in the working directory); allow disabling with an
      empty value.
  - Keep it simple; no database, no plotting tool (a plot script is out of scope
    for this issue).
  - Make logging non-blocking with the asyncio BLE loop (the notify handler must
    not block -- open the file in append mode and write a single line per event;
    do not hold the file open across awaits).
- **Backward compatibility:** old agent (no `start_notify`) simply ignores the
  notifications; new agent + old firmware (no ENV notify) just never receives ENV
  lines. Neither path breaks.
- Do NOT send ENV when the HAT is absent or the link is down.
- Do NOT change the existing metric (PC -> device) packet format.

**Dependencies:** PCP-005

**Expected result:**
While connected with the HAT attached, the device periodically notifies the
agent with ENV readings, and the agent logs them. Everything still works with
old/new mixes of agent and firmware.

**Acceptance criteria:**
- [ ] `docs/protocol.md` documents the `ENV;...` TX notify line
- [ ] Device sends `ENV;temp=..;hum=..;press=..` over TX notify on a slow cadence when connected + HAT present
- [ ] Agent subscribes via `start_notify` and parses ENV lines
- [ ] Agent appends timestamped rows to `env_log.csv` (header written once: `timestamp,temp_c,humidity_pct,pressure_hpa`)
- [ ] Agent echoes each reading to the console as a one-line summary
- [ ] CSV path configurable via CLI flag; logging can be disabled
- [ ] Opening `env_log.csv` in a spreadsheet shows the logged history
- [ ] Notify handler does not block the asyncio BLE loop
- [ ] No ENV sent when HAT absent or disconnected
- [ ] Existing PC -> device metric path unchanged
- [ ] Files changed: `firmware/pc_tamagotchi/pc_tamagotchi.ino`, `agent/pc_pet_agent.py`, `docs/protocol.md`

---

### PCP-010 -- ENV log retention (size-based rotation)

**Description:**
`env_log.csv` from PCP-009 grows without bound (~250 KB/day at a 5-10 s cadence).
Add size-based rotation so total disk usage stays bounded. This is an agent-only
follow-up to PCP-009 and does not touch the device. Rotation is append-only
friendly: it renames whole files (no in-place rewrite of the active log), which
is the safest approach while the agent is actively appending.

**What needs to be done:**
- Add two CLI flags to the agent:
  - `--env-log-max-bytes` (default `5_000_000`, ~5 MB) -- rotate when the active
    log reaches this size.
  - `--env-log-keep` (default `5`) -- how many rotated files to retain.
  - Total disk is bounded by `(keep + 1) * max_bytes` (~30 MB at defaults).
- Before appending each row (or after, then check), test the active file size.
  Prefer tracking a running byte counter updated on each write to avoid a
  `stat()` on every event; fall back to `os.path.getsize` on startup.
- When the active log reaches `--env-log-max-bytes`, rotate (logrotate style):
  1. If `env_log.csv.{keep}` exists, delete it (oldest).
  2. Shift `env_log.csv.{i}` -> `env_log.csv.{i+1}` for `i` from `keep-1` down to 1.
  3. `os.replace(env_log.csv, env_log.csv.1)` (atomic rename).
  4. Create a fresh `env_log.csv` and write the header row once.
  - Use `os.replace` / `os.rename` (atomic on the same filesystem); never copy
    + truncate the active file while appending.
- Derive rotated names from the configured log path (respect `--env-log` from
  PCP-009): `<path>` -> `<path>.1`, `<path>.2`, ...
- Handle edge cases:
  - Missing intermediate rotated files (gaps) -- skip cleanly.
  - Logging disabled (empty `--env-log`) -- retention is a no-op.
  - Rotation must not block the asyncio BLE loop (a handful of `os.replace`
    calls; keep it synchronous and fast, no awaits mid-rotation).
- Keep it dependency-free (stdlib `os` only) -- do NOT pull in
  `logging.handlers.RotatingFileHandler` unless it fits cleanly without
  reshaping the CSV writes (the file is a CSV with a header, not a log stream).
- Document the flags in the README agent section.
- Do NOT add compression (gzip) of rotated files -- out of scope for this issue
  (note it as a possible future enhancement).
- Do NOT add age-based pruning -- size rotation is the chosen policy.

**Dependencies:** PCP-009

**Expected result:**
`env_log.csv` rotates to `env_log.csv.1 .. .N` once it reaches the size cap,
the oldest rotated file is discarded, and total disk for ENV logs stays bounded.
Each fresh active log starts with the CSV header.

**Acceptance criteria:**
- [ ] `--env-log-max-bytes` and `--env-log-keep` flags added with sensible defaults
- [ ] Active log rotates to `.1` when it reaches the size cap
- [ ] Older rotated files shift up; the oldest beyond `keep` is deleted
- [ ] Rotation uses atomic `os.replace` (no in-place rewrite of the active log)
- [ ] Fresh active log after rotation starts with the CSV header row
- [ ] Total disk bounded by `(keep + 1) * max_bytes`
- [ ] Retention is a no-op when logging is disabled
- [ ] Rotation does not block the asyncio BLE loop
- [ ] No new dependencies (stdlib only)
- [ ] README documents the retention flags
- [ ] Files changed: `agent/pc_pet_agent.py`, `README.md`

---

## Stage 6 -- Tooling

### PCP-011 -- ENV CSV viewer TUI (separate application)

**Description:**
A standalone terminal application that reads `env_log.csv` (produced by PCP-009,
rotated by PCP-010) and visualizes room temperature, humidity, and pressure over
time as an interactive TUI. It is a **separate app** with its own dependencies
and its own entry point -- it does not import from or modify the agent or the
firmware. It only consumes the CSV on disk, so it can run on the same machine as
the agent or anywhere the CSV is copied.

**Stack:** Textual (interactive TUI framework) + plotext via `textual-plotext`
(terminal charts). Kept isolated in its own `requirements.txt` so the agent's
runtime stays lean.

**What needs to be done:**
- Create a new self-contained app directory `tools/env_viewer/`:
  ```
  tools/env_viewer/
    env_viewer.py        # entry point (Textual App)
    requirements.txt     # textual, textual-plotext
    README.md            # usage, screenshots/ascii, flags
  ```
- **Input:** read the PCP-009 CSV schema
  (`timestamp,temp_c,humidity_pct,pressure_hpa`).
  - Default path `env_log.csv`; override with `--log <path>`.
  - `--include-rotated` -- also read `<path>.1 .. <path>.N` from PCP-010 and
    stitch them in chronological order for full history (off by default).
  - Parse timestamps (ISO-8601); skip malformed rows without crashing.
- **TUI layout (Textual):**
  - A chart area showing one metric at a time (Temperature / Humidity /
    Pressure) rendered with a `textual-plotext` PlotextPlot widget (time on X,
    value on Y).
  - Tabs or a switch to change the active metric; the metric name and unit are
    shown.
  - A status/footer bar: total row count, time span covered, and the latest
    value of each metric.
  - Keyboard bindings: `q` quit, `tab` / left-right to switch metric, `r` manual
    refresh, and (optional) number keys to pick a time window.
- **Live refresh:** use Textual's `set_interval` to re-read the CSV on a timer
  (default ~5 s, `--refresh`), so rows appended by a running agent appear without
  restarting. Read incrementally / tail rather than reparsing the whole file each
  tick where practical.
- **Time window:** `--window` (e.g. `1h`, `24h`, `all`, default `24h`) to bound
  how much history is charted; keys to cycle windows at runtime. For large files,
  only load/plot the rows in the window (efficient on multi-MB logs).
- **Robustness:**
  - Missing or empty CSV -> show a friendly "waiting for data / no log found"
    state, not a stack trace.
  - File being appended/rotated mid-read -> tolerate transient read errors and
    retry on the next tick.
  - Degrade cleanly in small terminals.
- **Docs:** `tools/env_viewer/README.md` with install
  (`pip install -r requirements.txt`), run (`python env_viewer.py --log ...`),
  flags, and key bindings. Add a short pointer from the main README.
- Do NOT modify `agent/pc_pet_agent.py` or the firmware -- the viewer is
  read-only on the CSV.
- Do NOT add the viewer's deps to the agent `requirements.txt` -- keep them
  isolated in `tools/env_viewer/requirements.txt`.
- Do NOT have the viewer write to or rotate the CSV (read-only consumer).

**Dependencies:** PCP-009 (CSV format/source). Soft: PCP-010 (rotated files --
`--include-rotated` only matters once rotation exists).

**Expected result:**
Running `python tools/env_viewer/env_viewer.py` opens an interactive terminal
dashboard charting ENV history from `env_log.csv`, switchable between
temperature / humidity / pressure, auto-refreshing as the agent appends new
readings.

**Acceptance criteria:**
- [ ] Self-contained app under `tools/env_viewer/` with its own `requirements.txt`
- [ ] Reads the PCP-009 CSV schema; `--log` overrides the path
- [ ] `--include-rotated` stitches in `*.N` rotated files in chronological order
- [ ] Interactive Textual TUI charts temperature, humidity, and pressure (plotext)
- [ ] Metric switching via keyboard; status bar shows row count, span, latest values
- [ ] Live auto-refresh picks up newly appended rows (`--refresh` interval)
- [ ] `--window` bounds the charted history and stays efficient on multi-MB files
- [ ] Missing / empty / malformed CSV handled gracefully (no crash)
- [ ] Viewer is read-only -- never writes or rotates the CSV
- [ ] Viewer deps are isolated (not added to the agent requirements)
- [ ] `tools/env_viewer/README.md` documents install, usage, flags, key bindings
- [ ] Files changed: new `tools/env_viewer/*`, `README.md` (pointer)
