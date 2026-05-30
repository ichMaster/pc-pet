# Firmware internals -- pc_tamagotchi.ino

Detailed walkthrough of the device firmware. For the BLE packet format see
[protocol.md](protocol.md). For behavior from the user's perspective see the
README.

## File sections

| Section | Lines | Purpose |
|---------|-------|---------|
| Headers and UUIDs | 1--35 | Includes, NUS UUIDs, device name, MelNote struct |
| Shared state | 37--52 | Volatile globals guarded by `g_mux`, CPU history ring buffer |
| Mood enum and moodWord | 59--75 | `Mood` enum (M_-prefixed), mood-to-text with panic tier variants |
| Views and characters | 77--93 | View enum, character selection, g_view/g_char/g_mute state |
| Screen power management | 95--118 | Brightness constants, idle/dim/off thresholds, shake detection vars |
| Low-battery alert | 110--114 | LOW_BATT_PCT threshold, repeat interval |
| 1 Hz tick | 116--119 | g_lastTick1s, g_uptimeSec, g_panicSec |
| Canvas and BLE buffers | 121--142 | M5Canvas, g_rxBuf[200], g_rxReady flag, stashBytes() |
| BLE callbacks | 145--163 | RxCallbacks::onWrite (Bluedroid + NimBLE variants) |
| Packet parsing | 165--245 | parseProcList(), parsePacket() |
| Server callbacks | 247--256 | onConnect / onDisconnect |
| setup() | 259--324 | Board init, display, BLE service, advertising, splash |
| Melody system | 328--346 | Melody arrays and playMelody() |
| Helpers | 348--389 | lerpColor(), currentMood(), bodyColor(), panicTier() |
| Drawing | 391--579 | drawBar(), drawCharBody(), drawPet() |
| View renderers | 581--787 | renderTopBar(), viewPet(), viewStats(), viewGraph(), viewProcs() |
| Main loop | 789--952 | Shake, BLE drain, buttons, mood, power, render, delay |

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
  |               g_batt, g_charging, g_diskR, g_diskW
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
  v
drawPet(mood, frame) / viewStats() / viewGraph() / viewProcs()
  |  Renders to off-screen M5Canvas
  v
canvas.pushSprite(0, 0)
  |  Blits to display
  v
delay(55 ms)  or  delay(150 ms) if screen is off
```

## Thread safety

The firmware uses two tasks:

- **Bluetooth task** -- runs the BLE stack; fires `onWrite` when the PC writes
  a packet. Has a small stack, so the callback only copies bytes and sets a
  flag.
- **Main task** -- runs `loop()`. Does all parsing, mood computation, rendering,
  and Serial output.

Shared state between the two tasks is guarded by `g_mux` (a portMUX spinlock
used with `portENTER_CRITICAL` / `portEXIT_CRITICAL`). The proc-list arrays
(`g_cpuProcs`, `g_ramProcs`) are written and read only on the main task, so
they do not need the mutex.

## Global variables

### Metrics (volatile, mutex-guarded)

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
| g_connected | bool | BLE link status | onWrite / onDisconnect |
| g_lastPacket | uint32_t | millis() of last RX | onWrite |

### UI state (main task only)

| Variable | Purpose |
|----------|---------|
| g_view | Current screen (VIEW_PET / VIEW_STATS / VIEW_GRAPH / VIEW_PROCS) |
| g_char | Character index (0--4: Blobby, Cat, Robo, Ghost, Bunny) |
| g_mute | Audio mute flag |
| g_frame | Animation frame counter |
| g_prevMood | Previous mood (for edge-triggered alerts) |
| g_panicSec | Continuous seconds in M_PANIC (drives tier escalation) |
| g_uptimeSec | Seconds since boot |
| g_lastActivity | millis() of last user interaction (drives screen dim/off) |
| g_forceOff | User manually turned screen off via power button |
| g_curBri | Cached brightness to avoid redundant setBrightness calls |

### Shake detection (main task only)

| Variable | Purpose |
|----------|---------|
| g_lax, g_lay, g_laz | Last accelerometer sample |
| g_accelInit | Whether first sample has been taken |
| g_shakeStart | millis() when current shake burst began |
| g_lastShake | millis() of last strong shake sample |

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

When disconnected, mood is forced to M_SLEEP.

### Body colors (bodyColor)

| Mood | RGB | Description |
|------|-----|-------------|
| M_SLEEP | (120, 150, 230) | Soft blue |
| M_HAPPY | (120, 215, 140) | Green |
| M_BUSY | (245, 190, 70) | Orange-yellow |
| M_STUFFED | (200, 160, 120) | Brown |
| M_HOT | (255, 130, 70) | Red-orange |
| M_PANIC | (245, 90, 80) | Red |
| M_LOWPWR | (150, 140, 160) | Muted purple-gray |

### Panic tiers (panicTier)

| Tier | Duration | Body color shift | Jitter | Sweat | Eyes | Mouth | Sound | Text |
|------|----------|-----------------|--------|-------|------|-------|-------|------|
| 1 | 0--9 s | standard red | +/-2 px | 2 drops | wide, small pupils | open O | MEL_ALERT on entry | PANIC!! |
| 2 | 10--29 s | 50% toward (180,50,40) | +/-4 px | 4 drops | wide, small pupils | open O | MEL_PANIC2 | PANIC!!! |
| 3 | 30+ s | 70% toward (140,120,130) | +/-4 px | 4 drops | half-closed (lids) | wavy sine | MEL_PANIC3 | CRITICAL |

Reverts instantly when panic clears (g_panicSec resets to 0).

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
| Pulsing red overlay | M_PANIC tier 3; sine-driven opacity on inner ellipse |

## Character system (drawCharBody)

5 characters, cycled with BtnB single-click. Only the body silhouette
differs; eyes, mouth, and effects are shared.

| ID | Name | Silhouette | Special features |
|----|------|-----------|-----------------|
| 0 | Blobby | Ellipse | Small white highlight (shine) |
| 1 | Cat | Ellipse + pointy ears | Pink inner ears, whiskers |
| 2 | Robo | Rounded rectangle | Antenna with red cap, dark panel line, sensor dots |
| 3 | Ghost | Ellipse + wavy bottom | 4 bumps (ghost feet), rectangular body overlay |
| 4 | Bunny | Ellipse + tall ears | Pink inner ears |

## Views

### Top bar (renderTopBar, all views)

18 px header:
- Left: green dot + "BLE" (connected) or red dot + ".." (disconnected)
- Center: character name, or "mute" in orange if muted
- Right: device battery % (red "! X%" if below 10%)

### VIEW_PET (viewPet)

Main screen. Background has a subtle mood-tinted gradient. Shows the animated
creature at center, mood word below, busiest process name, CPU and RAM bars,
and a footer with temperature, GPU %, and PC battery.

### VIEW_STATS (viewStats)

Four stat rows with labeled bars: CPU, RAM, GPU, TEMP. Below: battery status
(with charging indicator), disk read/write MB/s, network KB/s + process count,
and top process name.

### VIEW_GRAPH (viewGraph)

Scrolling CPU history line graph. 110-sample ring buffer displayed as a
connected line. Grid lines at 25%, 50%, 75%. Current CPU % shown in the
top-right corner.

### VIEW_PROCS (viewProcs)

Split screen: top 4 CPU-heavy processes (yellow header) on the left, top 4
RAM-heavy processes (blue header) on the right. Each shows process name and
percentage.

## Screen power management

| State | Trigger | Brightness | Rendering |
|-------|---------|------------|-----------|
| Full | activity detected | 110 | yes, 55 ms delay (~18 fps) |
| Dim | 10 s idle | 55 | yes, 55 ms delay |
| Off | 30 s idle or forceOff | 0 | skipped, 150 ms delay (~6.7 fps) |

Activity sources that reset the idle timer:
- Any button press (BtnA, BtnB, Power)
- Sustained shake (acceleration delta > 1.2 for 750+ ms)
- Alert mood (M_HOT or M_PANIC keep the screen fully awake)
- Low-battery alert firing

## Button handling

| Button | Event | Action | Sound |
|--------|-------|--------|-------|
| BtnA (front) | click | cycle view | MEL_CLICK (1500 Hz) |
| BtnB (side) | single click | next character | MEL_CHARSWITCH (1700 Hz) |
| BtnB (side) | double click | toggle mute | MEL_MUTE_OFF (1800 Hz) on unmute |
| Power (lower) | short press | toggle screen on/off | none |
| Power (lower) | hold ~1 s | power off device | none |

## Low-battery alert

Fires when the M5Stick's own battery drops below 10%:
- Plays MEL_LOWBATT (800 Hz descending to 600 Hz)
- Wakes the screen
- Repeats every 2 minutes while battery stays low

## Melody system

All sounds go through `playMelody()`, which checks `g_mute` before playing.
Each melody is a null-terminated array of `MelNote { freq, durMs, pauseMs }`.

| Melody | Notes | When |
|--------|-------|------|
| MEL_CLICK | 1500 Hz, 30 ms | BtnA press |
| MEL_CHARSWITCH | 1700 Hz, 30 ms | BtnB single click |
| MEL_MUTE_OFF | 1800 Hz, 40 ms | Unmuting |
| MEL_ALERT | 2300 Hz x2 | Mood escalates to PANIC or HOT |
| MEL_PANIC2 | 4-note 2000/2400 Hz | Panic reaches tier 2 (10 s) |
| MEL_PANIC3 | 5-note 2200--2800 Hz | Panic reaches tier 3 (30 s) |
| MEL_LOWBATT | 800 Hz -> 600 Hz | Device battery below 10% |
| MEL_LOWPWR | 1200 Hz -> 900 Hz | (defined, not yet wired) |
| MEL_MUTE_ON | 600 Hz, 40 ms | (defined, not used -- muting is silent) |
| MEL_PANIC1 | 2-note 2000/2400 Hz | (defined, reserved for future use) |

## Constants reference

### BLE

| Constant | Value |
|----------|-------|
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
| PANIC_T1 | 10 s |
| PANIC_T2 | 30 s |

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

### Animation

| Constant | Value |
|----------|-------|
| Frame delay (awake) | 55 ms (~18 fps) |
| Frame delay (off) | 150 ms (~6.7 fps) |
| Breathing speed | frame * 0.18 |
| Blink interval | every ~90 frames |
| HIST (graph samples) | 110 |
| NPROC (top processes) | 4 |
