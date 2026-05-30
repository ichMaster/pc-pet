# Firmware internals -- pc_tamagotchi

Detailed walkthrough of the device firmware. For the BLE packet format see
[protocol.md](protocol.md). For behavior from the user's perspective see the
README.

The sketch is split across four Arduino tabs in the
`firmware/pc_tamagotchi/` folder. The Arduino IDE compiles every `.ino` in the
sketch folder as one translation unit (alphabetical order, the main `.ino`
first), so functions defined in one tab are visible from the others.

## File layout

| File | Role | Key contents |
|------|------|--------------|
| `pet_types.h` | Shared type definitions | `MelNote` struct, `Mood` enum, `View` enum, `EnvMod` enum, `ProcEntry` struct |
| `pc_tamagotchi.ino` | Main sketch | Includes, NUS UUIDs, all globals, BLE callbacks, packet parsing, `setup()`, melody data, `loop()` |
| `pet_helpers.ino` | Logic helpers | `playMelody()`, `lerpColor()`, `currentMood()`, `bodyColor()`, `panicTier()`, `pressTrend()`, `envModifier()`, `drawBar()` |
| `pet_render.ino` | Character art + screens | `drawCharBody()`, `drawPet()`, `renderTopBar()`, `viewPet()`, `viewStats()`, `viewGraph()`, `viewProcs()`, `viewEnv()` |

### `pet_types.h`

Pure declarations, no code:

- `MelNote { uint16_t freq, durMs, pauseMs }` -- one note in a melody array.
- `Mood` -- `M_SLEEP, M_HAPPY, M_BUSY, M_STUFFED, M_HOT, M_PANIC, M_LOWPWR`.
  The `M_` prefix is mandatory: the ESP32 ROM headers already define `BUSY` and
  `HOT` in their own enum, so unprefixed names will not compile.
- `View` -- `VIEW_PET, VIEW_STATS, VIEW_GRAPH, VIEW_PROCS, VIEW_ENV, VIEW_COUNT`.
- `EnvMod` -- `ENV_NONE, ENV_STUFFY, ENV_WEATHER` (environment mood modifier).
- `ProcEntry { char name[14]; int val; }` -- one row in a process list.

### `pc_tamagotchi.ino`

Includes M5Unified, the BLE stack (BLEDevice / BLEServer / BLEUtils / BLE2902),
`math.h`, `M5UnitENV.h`, and `pet_types.h`. Contains:

- Nordic UART (NUS) UUIDs: `SERVICE_UUID`, `CHAR_RX_UUID` (PC -> device write),
  `CHAR_TX_UUID` (device -> PC notify, used for ENV telemetry).
- All global state (see [Global variables](#global-variables)).
- `stashBytes()` and the `RxCallbacks` class (BLE write callback).
- `parseProcList()` and `parsePacket()` (packet decoding).
- `ServerCallbacks` (connect / disconnect).
- `setup()` -- board, display, canvas, ENV III I2C, and BLE bring-up.
- Melody definitions (`MEL_*` arrays).
- `loop()` -- the main render / logic loop.

### `pet_helpers.ino`

Pure logic, no display reads. `playMelody()`, `lerpColor()`, `currentMood()`,
`bodyColor()`, `panicTier()`, `pressTrend()`, `envModifier()`, and `drawBar()`.

### `pet_render.ino`

All drawing. `drawCharBody()` and `drawPet()` render the creature;
`renderTopBar()` plus the five `view*()` functions render the five screens.

## Data flow

```
Agent (Python, every 1.5 s)
  |
  |  BLE Write Request (NUS RX characteristic)
  v
RxCallbacks::onWrite()          <- runs on the Bluetooth task (small stack)
  |  stashBytes(): copy raw bytes into g_rxBuf, set g_rxReady
  |  (no parsing, no Serial.printf -- stack is too small)
  v
loop() on main task
  |  if (g_rxReady): copy g_rxBuf to local buffer under mutex, clear flag
  |  parsePacket(): split by ";", then by ","
  |    metrics -> g_cpu, g_ram, g_temp, g_net, g_procs, g_top, g_gpu,
  |               g_batt, g_charging, g_diskR, g_diskW   (indices 0..10)
  |    cpuList -> g_cpuProcs[]
  |    ramList -> g_ramProcs[]
  |    g_hist[g_histPos] = cpu (ring buffer for graph)
  |    g_connected = true, g_lastPacket = millis()
  v
Snapshot (portENTER_CRITICAL)
  |  Copy all g_* into local variables
  |  Stale check: if no packet for 6 s, connected = false
  v
currentMood(cpu, ram, temp, gpu, batt, charging)
  |  Returns Mood enum
  |  envModifier() may override to M_STUFFED when the PC mood is calm
  v
viewPet() / viewStats() / viewGraph() / viewProcs() / viewEnv()
  |  drawPet() etc. render to off-screen M5Canvas
  v
canvas.pushSprite(0, 0)
  |  Blits to display
  v
delay(55 ms)  or  delay(150 ms) if screen is off
```

Two background data paths run independently of the PC link:

```
ENV III sensors (every ~2 s, blocking I2C on Wire)
  g_sht30.update()    -> g_envTemp, g_envHum
  g_qmp6988.update()  -> g_envPress (Pa -> hPa)
  every ~60 s: append g_envPress to g_pressHist ring (pressure trend)
  pressTrend() compares newest vs oldest sample -> +1 / 0 / -1
  envModifier() -> ENV_STUFFY / ENV_WEATHER / ENV_NONE

ENV telemetry (every ~5 s, only if connected + hat present)
  format "ENV;temp=%.1f;hum=%d;press=%d"
  g_txChar->setValue(...); g_txChar->notify()   -> PC agent
```

## Thread safety

The firmware uses two tasks:

- **Bluetooth task** -- runs the BLE stack; fires `onWrite` when the PC writes
  a packet. Has a small stack, so the callback only copies bytes and sets a
  flag (`stashBytes()`).
- **Main task** -- runs `loop()`. Does all parsing, mood computation, ENV
  sensor reads, rendering, telemetry notifies, and Serial output.

Shared state between the two tasks is guarded by `g_mux` (a portMUX spinlock
used with `portENTER_CRITICAL` / `portEXIT_CRITICAL`). The proc-list arrays
(`g_cpuProcs`, `g_ramProcs`), CPU history ring, all UI / power state, and all
ENV state are written and read only on the main task, so they do not need the
mutex. The ENV telemetry notify is sent from the main task only, so `g_txChar`
is never written from the BLE callback.

## Global variables

### Metrics (mutex-guarded, written by BLE task via parse on main task)

| Variable | Type | Range | Source |
|----------|------|-------|--------|
| g_cpu | int | 0--100 | psutil.cpu_percent |
| g_ram | int | 0--100 | psutil.virtual_memory |
| g_temp | int | Celsius, -1 = unknown | macmon / psutil |
| g_net | int | KB/s, 0--9999 | psutil.net_io_counters |
| g_procs | int | process count | psutil.pids |
| g_gpu | int | 0--100, -1 = unknown | macmon |
| g_batt | int | 0--100, -1 = none | psutil.sensors_battery |
| g_charging | int | 0 or 1 | psutil.sensors_battery |
| g_diskR | int | MB/s, 0--9999 | psutil.disk_io_counters |
| g_diskW | int | MB/s, 0--9999 | psutil.disk_io_counters |
| g_top[16] | char[] | busiest process name | psutil.process_iter |
| g_connected | bool | BLE link status | onConnect / onDisconnect |
| g_lastPacket | uint32_t | millis() of last RX | parsePacket |
| g_txChar | BLECharacteristic* | TX notify handle | setup() |

### Process lists (main task only)

| Variable | Purpose |
|----------|---------|
| g_cpuProcs[NPROC] / g_cpuProcN | Top CPU processes (name + %), count |
| g_ramProcs[NPROC] / g_ramProcN | Top RAM processes (name + %), count |

### CPU history (main task only)

| Variable | Purpose |
|----------|---------|
| g_hist[HIST=110] | uint8_t ring buffer of CPU % samples (graph) |
| g_histPos | Write position in the ring |

### UI state (main task only)

| Variable | Purpose |
|----------|---------|
| g_view | Current screen (`View` enum) |
| g_char | Character index (0--4: Blobby, Cat, Robo, Ghost, Bunny) |
| g_mute | Audio mute flag |
| g_frame | Animation frame counter |
| g_prevMood | Previous mood (for edge-triggered alerts) |

### Screen power management (main task only)

| Variable | Purpose |
|----------|---------|
| g_lastActivity | millis() of last user interaction (drives dim/off) |
| g_curBri | Cached brightness to avoid redundant setBrightness calls |
| g_forceOff | User manually turned screen off via power button |
| g_lax, g_lay, g_laz | Last accelerometer sample |
| g_accelInit | Whether first accel sample has been taken |
| g_shakeStart | millis() when the current shake burst began |
| g_lastShake | millis() of last strong shake sample |

### Low-battery alert (main task only)

| Variable | Purpose |
|----------|---------|
| g_battWasLow | Edge-detect flag for first crossing below threshold |
| g_lastBattBeep | millis() of last low-battery beep (throttle) |

### 1 Hz tick (main task only)

| Variable | Purpose |
|----------|---------|
| g_lastTick1s | Debounce gate for 1 Hz events |
| g_uptimeSec | Seconds since boot |
| g_panicSec | Continuous seconds in M_PANIC (drives tier escalation) |

### ENV III HAT (main task only, gated by g_envPresent)

| Variable | Purpose |
|----------|---------|
| g_sht30 | SHT3X temperature + humidity sensor object (Wire) |
| g_qmp6988 | QMP6988 barometric pressure sensor object (Wire) |
| g_envPresent | True only if both sensors initialized at boot |
| g_envTemp | Room temperature (float, C) |
| g_envHum | Relative humidity (float, %) |
| g_envPress | Barometric pressure (float, hPa) |
| g_lastEnvRead | Gate for sensor reads (~2 s) |

### Pressure trend (main task only)

| Variable | Purpose |
|----------|---------|
| g_pressHist[PRESS_HIST=60] | Slow ring buffer of pressure samples (float) |
| g_pressHistN | Count of valid samples (0..60) |
| g_pressHistPos | Ring write position |
| g_lastPressLog | Gate for the ~1/minute pressure log |

### ENV telemetry (main task only)

| Variable | Purpose |
|----------|---------|
| g_lastEnvSend | Gate for the ~5 s device -> PC notify |

### BLE RX buffer (BLE callback task)

| Variable | Purpose |
|----------|---------|
| g_rxReady | volatile flag: a packet is waiting |
| g_rxBuf[200] | Raw packet bytes |
| g_rxLen | volatile byte count |
| g_writeCount, g_dbgGetLen, g_dbgParamLen | Debug counters |

### Canvas

| Variable | Purpose |
|----------|---------|
| canvas | M5Canvas sprite buffer; everything renders here then pushSprite |

## Mood system

### Decision tree (currentMood)

Evaluated top to bottom; first match wins:

| Priority | Condition | Mood |
|----------|-----------|------|
| 1 | temp >= 75 C | M_HOT |
| 2 | cpu >= 85 OR ram >= 92 OR gpu >= 95 | M_PANIC |
| 3 | battery 0--19% and not charging | M_LOWPWR |
| 4 | ram >= 85 | M_STUFFED |
| 5 | cpu >= 50 OR gpu >= 60 | M_BUSY |
| 6 | cpu < 15 AND gpu < 15 | M_SLEEP |
| 7 | (default) | M_HAPPY |

When the PC link is stale (no packet for 6 s), the snapshot reports
disconnected and the metrics decay toward the calm path.

### Environment override

When the ENV III HAT is present and the PC mood is the calm default
(`M_HAPPY`), `envModifier()` may adjust the displayed mood:

- `ENV_STUFFY` (room temp >= 27 C and humidity >= 60%) overrides the mood to
  `M_STUFFED` and shows a "stuffy" badge in the top-left of VIEW_PET.
- `ENV_WEATHER` (barometric pressure falling) leaves the mood alone but shows a
  "weather" badge.

The override is applied only on top of `M_HAPPY`; any active PC-driven alert
mood (HOT, PANIC, etc.) takes precedence. With no hat, `envModifier()` always
returns `ENV_NONE`.

### Body colors (bodyColor)

| Mood | RGB | 565 | Description |
|------|-----|-----|-------------|
| M_SLEEP | (120, 150, 230) | 0x6F9E | Soft blue |
| M_HAPPY | (120, 215, 140) | 0x78D8 | Green |
| M_BUSY | (245, 190, 70) | 0xF5C6 | Orange-yellow |
| M_STUFFED | (200, 160, 120) | 0xC8A0 | Brown |
| M_HOT | (255, 130, 70) | 0xFF43 | Red-orange |
| M_PANIC | (245, 90, 80) | 0xF541 | Red |
| M_LOWPWR | (150, 140, 160) | 0x968A | Muted purple-gray |

### Panic tiers (panicTier)

`g_panicSec` counts continuous seconds in `M_PANIC` (incremented on the 1 Hz
tick). `panicTier(sec)` maps it to tier 1/2/3:

| Tier | Duration | Body color shift | Jitter | Sweat | Eyes | Mouth | Sound | Text |
|------|----------|-----------------|--------|-------|------|-------|-------|------|
| 1 | 0--9 s | standard red | +/-2 px | 2 drops | wide, small pupils | open O | MEL_ALERT on entry | PANIC!! |
| 2 | 10--29 s | 50% toward (180,50,40) | +/-4 px | 4 drops | wide, small pupils | open O | MEL_PANIC2 | PANIC!!! |
| 3 | 30+ s | 70% toward (140,120,130) | +/-4 px | 4 drops | half-closed (lids) | wavy sine | MEL_PANIC3 | CRITICAL |

Tier transitions are edge-triggered on the 1 Hz tick: when
`panicTier(g_panicSec) != panicTier(g_panicSec - 1)`, the matching melody plays
once. Reverts instantly when panic clears (`g_panicSec` resets to 0).

## Animation details (drawPet)

All characters share the same mood-driven expressions. Frame-based, using
`g_frame` incremented each loop iteration.

### Motion

| Mood | Animation |
|------|-----------|
| All | Breathing: body rx/ry scale by `1.0 + 0.05 * sin(frame * 0.18)` |
| M_HAPPY | Gentle vertical bounce: `2 * sin(t)` |
| M_BUSY | Faster bounce: `3 * |sin(t * 1.6)|`, plus alternating leg step |
| M_PANIC | Horizontal jitter: +/-2 px (tier 1) or +/-4 px (tier 2--3) |

### Eyes

| Mood | Style |
|------|-------|
| M_SLEEP | Closed curved arcs |
| M_HAPPY / M_BUSY | Round with pupils; blinks every ~90 frames |
| M_BUSY | Pupils dart left/right every 12 frames |
| M_PANIC (tier 1--2) | Wide (+2 px radius), small pupils (2 px vs 3 px) |
| M_PANIC (tier 3) | Half-closed with heavy lids (reuses M_LOWPWR style) |
| M_HOT | Wide (+2 px radius) |
| M_LOWPWR | Half-closed, small pupils, heavy colored lids |

### Mouth

| Mood | Shape |
|------|-------|
| M_HAPPY | Arc smile |
| M_SLEEP | Small ellipse (sleeping mouth) |
| M_BUSY | Small filled circle (O shape) |
| M_STUFFED | Straight horizontal line |
| M_LOWPWR | Slight downward frown arc |
| M_HOT / M_PANIC (tier 1--2) | Large open ellipse with red inner |
| M_PANIC (tier 3) | Wavy sine-driven line (animated dizzy mouth) |

### Effects

| Effect | Condition |
|--------|-----------|
| Sweat drops | M_HOT or M_PANIC; 2 drops base, 4 at tier 2+ |
| Floating Zzz | M_SLEEP; two "z"/"Z" floating upward |
| Stuffed cheeks | M_STUFFED; small pink circles on sides |
| Pulsing red overlay | M_PANIC tier 3; sine-driven opacity (0.15--0.25) |

## Character system (drawCharBody)

5 characters, cycled with BtnB single-click. Only the body silhouette
differs; eyes, mouth, and effects are shared.

| ID | Name | Silhouette | Special features |
|----|------|-----------|-----------------|
| 0 | Blobby | Rounded ellipse | Small white highlight (shine) |
| 1 | Cat | Ellipse + pointy ears | Pink inner ears, whiskers |
| 2 | Robo | Rounded rectangle | Antenna with red cap, dark panel line, sensor dots |
| 3 | Ghost | Ellipse + wavy bottom | Bumpy ghost feet, rectangular body overlay |
| 4 | Bunny | Ellipse + tall ears | Pink inner ears |

`CHAR_COUNT = 5`; `charName(c)` returns the display name.

## Views

The screen is portrait 135x240. `g_view` selects one of five screens; BtnA
cycles through them. The render dispatch in `loop()` calls the matching
`view*()` function, which builds the frame on `canvas` and the loop then calls
`canvas.pushSprite(0, 0)`.

### Top bar (renderTopBar, all data views)

18 px header:
- Left: green dot + "BLE" (connected) or red dot + "..." (disconnected)
- Center: character name, or "mute" in orange if muted
- Right: device battery % (red, with "!" if below 10%)

### VIEW_PET (viewPet)

Main screen. Background has a subtle mood-tinted blend. Shows the animated
creature at center (y=96), the mood word below (tier-dependent text when
panicking), the busiest process name, CPU (yellow) and RAM (cyan) mini-bars,
and a footer with temperature, GPU %, and PC battery. When the ENV HAT is
present, a small "stuffy" or "weather" badge appears in the top-left (see
[Environment override](#environment-override)).

### VIEW_STATS (viewStats)

Title "Stats", then four labeled bar rows: CPU, RAM, GPU, TEMP (each with label,
value, and fill bar). Below: battery status with charging indicator, disk I/O
(read/write MB/s), network KB/s + process count, and the top process name.

### VIEW_GRAPH (viewGraph)

Scrolling CPU history line graph. The 110-sample `g_hist` ring buffer is plotted
oldest -> newest left to right. Bordered plot area with grid lines at 25%, 50%,
75%; current CPU % shown near the axis. ~110 x 55 ms ~= 6 s of history.

### VIEW_PROCS (viewProcs)

Two stacked sections: "TOP CPU" header + up to 4 processes (name left, % right),
a separator line, then "TOP RAM" header + up to 4 processes. Shows "(waiting)"
when connected but no list has arrived, or "(no link)" when disconnected.

### VIEW_ENV (viewEnv)

Environment screen, **only reachable when `g_envPresent` is true** -- BtnA skips
this view entirely when no hat is detected. Renders the top bar, an "ENV" title,
and three data rows:

- temp -- `"%.1f C"`, orange label
- humidity -- `"%d %%"`, cyan label
- pressure -- `"%d hPa"` plus a trend word from `pressTrend()`:
  "rising" / "falling" / "steady", or "--" when fewer than 3 samples have been
  logged

## ENV III HAT

The optional M5 ENV III HAT carries two I2C sensors on a second bus.

| Item | Detail |
|------|--------|
| Bus | `Wire` on GPIO 0 (SDA) / GPIO 26 (SCL), 400 kHz |
| SHT3X (SHT30) | Temperature + humidity, addr 0x44 -> `g_envTemp`, `g_envHum` |
| QMP6988 | Barometric pressure, addr 0x76 -> `g_envPress` (Pa -> hPa) |
| Presence | `setup()` sets `g_envPresent` only if **both** sensors init OK |

If either sensor fails to initialize, `g_envPresent` stays false and the ENV
screen, ENV telemetry, and ENV mood modifier are all disabled.

### Sensor reads

`loop()` reads the sensors when `millis() - g_lastEnvRead >= 2000` (~2 s). The
I2C reads are synchronous and block the loop, so they are gated rather than run
every frame. `g_sht30.update()` fills temperature and humidity;
`g_qmp6988.update()` fills pressure (converted to hPa).

### Pressure trend (pressTrend)

Roughly once a minute (`millis() - g_lastPressLog >= PRESS_LOG_MS`, 60000 ms),
the current `g_envPress` is appended to the `g_pressHist[60]` ring buffer.
`pressTrend()` compares the newest sample against the oldest:

- returns +1 when the delta is greater than +0.5 hPa (rising)
- returns -1 when the delta is below -0.5 hPa (falling)
- returns 0 within the +/-0.5 hPa band (steady), or when fewer than 3 samples
  have been logged

### ENV modifier (envModifier)

Returns an `EnvMod`:

1. `ENV_NONE` if `g_envPresent` is false.
2. `ENV_STUFFY` if `g_envTemp >= 27 C` AND `g_envHum >= 60%`.
3. `ENV_WEATHER` if `pressTrend() < 0` (falling pressure).
4. `ENV_NONE` otherwise.

Thresholds are hardcoded (`ENV_STUFFY_TEMP = 27.0`, `ENV_STUFFY_HUM = 60.0`).
See [Environment override](#environment-override) for how this affects mood.

### ENV telemetry (device -> PC)

When `g_envPresent`, the link is connected, and `g_txChar` is valid, `loop()`
notifies the PC every `ENV_SEND_MS` (5000 ms) with an ASCII line:

```
ENV;temp=%.1f;hum=%d;press=%d        e.g. ENV;temp=22.5;hum=55;press=1013
```

Sent via `g_txChar->setValue(...)` then `notify()`. The PC agent receives it on
the NUS TX characteristic (`CHAR_TX_UUID`) and can log it to a CSV.

## Screen power management

| State | Trigger | Brightness | Rendering |
|-------|---------|------------|-----------|
| Full | activity detected | 110 | yes, 55 ms delay (~18 fps) |
| Dim | 10 s idle | 55 | yes, 55 ms delay |
| Off | 30 s idle or forceOff | 0 | skipped, 150 ms delay (~6.7 fps) |

Activity sources that reset the idle timer:
- Any button press (BtnA, BtnB, Power)
- Sustained shake (acceleration delta > 1.2 for 750+ ms)
- Alert mood (M_HOT or M_PANIC force `g_lastActivity = now`, keeping the screen
  fully awake)
- Low-battery alert firing

When the screen is off the view dispatch is skipped and the loop runs at the
slower 150 ms cadence.

## Button handling

| Button | Event | Action | Sound |
|--------|-------|--------|-------|
| BtnA (front) | click | cycle view (skips VIEW_ENV when no hat) | MEL_CLICK (1500 Hz) |
| BtnB (side) | single click | next character | MEL_CHARSWITCH (1700 Hz) |
| BtnB (side) | double click | toggle mute | MEL_MUTE_OFF (1800 Hz) on unmute |
| Power (lower) | short press | toggle screen on/off | none |
| Power (lower) | hold ~1 s | power off device | none |

## Low-battery alert

Fires when the M5Stick's own battery drops below 10%:
- Plays MEL_LOWBATT (800 Hz descending to 600 Hz)
- Wakes the screen (`g_lastActivity = now`)
- Repeats every 2 minutes (`LOW_BATT_REPEAT`) while battery stays low
  (`g_battWasLow` edge-detects the first crossing)

## Melody system

All sounds go through `playMelody()`, which checks `g_mute` before playing.
Each melody is a null-terminated array of `MelNote { freq, durMs, pauseMs }`.

| Melody | Notes | When |
|--------|-------|------|
| MEL_CLICK | 1500 Hz, 30 ms | BtnA press |
| MEL_CHARSWITCH | 1700 Hz, 30 ms | BtnB single click |
| MEL_MUTE_OFF | 1800 Hz, 40 ms | Unmuting |
| MEL_MUTE_ON | 600 Hz, 40 ms | (defined; muting is currently silent) |
| MEL_ALERT | 2300 Hz x2 | Mood escalates to PANIC or HOT |
| MEL_PANIC1 | 2-note 2000/2400 Hz | (defined, reserved) |
| MEL_PANIC2 | 4-note 2000/2400 Hz | Panic reaches tier 2 (10 s) |
| MEL_PANIC3 | 5-note 2200--2800 Hz | Panic reaches tier 3 (30 s) |
| MEL_LOWBATT | 800 Hz -> 600 Hz | Device battery below 10% |
| MEL_LOWPWR | 1200 Hz -> 900 Hz | (defined, not yet wired) |

## Constants reference

### BLE

| Constant | Value |
|----------|-------|
| SERVICE_UUID | 6E400001-B5A3-F393-E0A9-E50E24DCCA9E (NUS) |
| CHAR_RX_UUID | 6E400002-...DCCA9E (PC -> device write) |
| CHAR_TX_UUID | 6E400003-...DCCA9E (device -> PC notify) |
| MTU | 185 bytes |
| RX buffer | 200 bytes |
| Stale link timeout | 6 s |
| Device name | "PCpet" |

### Mood thresholds

| Threshold | Value |
|-----------|-------|
| temp (HOT) | >= 75 C |
| cpu (PANIC) | >= 85% |
| ram (PANIC) | >= 92% |
| gpu (PANIC) | >= 95% |
| battery (LOWPWR) | < 20% and not charging |
| ram (STUFFED) | >= 85% |
| cpu (BUSY) | >= 50% |
| gpu (BUSY) | >= 60% |
| cpu + gpu (SLEEP) | both < 15% |

### Panic tiers

| Constant | Value |
|----------|-------|
| PANIC_T1 | 10 s (tier 1 -> 2) |
| PANIC_T2 | 30 s (tier 2 -> 3) |

### Screen power

| Constant | Value |
|----------|-------|
| FULL_BRI | 110 |
| DIM_BRI | 55 |
| DIM_AFTER_MS | 10000 (10 s) |
| OFF_AFTER_MS | 30000 (30 s) |
| SHAKE_THRESH | 1.2 |
| SHAKE_HOLD_MS | 750 |

### Low-battery alert

| Constant | Value |
|----------|-------|
| LOW_BATT_PCT | 10% |
| LOW_BATT_REPEAT | 120000 ms (2 min) |

### ENV III

| Constant | Value |
|----------|-------|
| Wire pins | SDA = GPIO 0, SCL = GPIO 26 |
| SHT30 address | 0x44 |
| QMP6988 address | 0x76 |
| Sensor read gate | ~2000 ms |
| ENV_STUFFY_TEMP | 27.0 C |
| ENV_STUFFY_HUM | 60.0 % |
| PRESS_HIST | 60 samples |
| PRESS_LOG_MS | 60000 ms (1 min) |
| Pressure trend band | +/-0.5 hPa |
| ENV_SEND_MS | 5000 ms (telemetry notify) |

### Animation

| Constant | Value |
|----------|-------|
| Frame delay (awake) | 55 ms (~18 fps) |
| Frame delay (off) | 150 ms (~6.7 fps) |
| Breathing speed | frame * 0.18 |
| Blink interval | every ~90 frames |
| HIST (graph samples) | 110 |
| NPROC (top processes) | 4 |
| CHAR_COUNT (characters) | 5 |
