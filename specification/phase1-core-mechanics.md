# Phase 1 -- Core mechanics (stages 1-4)

Foundation + first real gameplay features. All device-side except disk I/O.

| Stage | Content | Issue | Effort |
|---|---|---|---|
| 1 | 1.1 tick -- 1 Hz `millis()`-based counter | [#1](https://github.com/ichMaster/pc-pet/issues/1) | low |
| 2 | 8a buzzer patterns -- per-event melody signatures | [#2](https://github.com/ichMaster/pc-pet/issues/2) | low |
| 3 | #2 panic tiers -- 3 escalating states by duration | [#3](https://github.com/ichMaster/pc-pet/issues/3) | low-medium |
| 4 | #6 disk I/O -- read/write MB/s in agent + Stats row | [#4](https://github.com/ichMaster/pc-pet/issues/4) | low |

---

## Stage 1: One-second tick (1 Hz millis-based counter)

**Issue:** [#1](https://github.com/ichMaster/pc-pet/issues/1)
**Effort:** low
**Labels:** firmware
**Dependencies:** none (required by stage 3)

### Summary

Add a stable 1 Hz time base to the firmware. The current frame rate varies
(~18 fps / 55 ms when the screen is on, ~6.7 fps / 150 ms when off), so
frame-counting is unreliable for measuring real-world durations. This tick is
the foundation for panic tiers (stage 3).

### Implementation details

#### New globals (declare above `setup()`, per project convention)

```cpp
uint32_t g_lastTick1s = 0;   // last time the 1 s tick fired
uint32_t g_uptimeSec  = 0;   // seconds since boot (informational)
uint32_t g_panicSec   = 0;   // continuous seconds in M_PANIC (used by stage 3)
```

#### Tick logic in `loop()`

Insert a new block right after the mood computation (~line 808), before screen
power management:

```cpp
// ---- 1 Hz tick ----
if (millis() - g_lastTick1s >= 1000) {
  g_lastTick1s = millis();
  g_uptimeSec++;

  // panic duration counter (stage 3 will add tier logic here)
  if (mood == M_PANIC)
    g_panicSec++;
  else
    g_panicSec = 0;
}
```

Key points:
- Use `millis()` delta, not `delay()` -- non-blocking.
- `g_panicSec` resets instantly when panic clears (instant revert, per roadmap).
- Placement after mood computation ensures the tick sees the current mood.
- No mutex needed -- all reads/writes are on the main task.

#### What NOT to do

- Do not use `g_frame` for duration measurement (it drifts with screen state).
- Do not add FreeRTOS timers or separate tasks -- keep everything on `loop()`.
- Do not persist `g_uptimeSec` (no NVS in scope).

### Testing

- Upload, open Serial Monitor at 115200.
- Add a temporary `Serial.printf` inside the tick block:
  `Serial.printf("tick: uptime=%lu panic=%lu\n", g_uptimeSec, g_panicSec);`
- Confirm it prints once per second regardless of screen on/off.
- Run `tools/cpu_stress.py --percent 90` to trigger panic, verify `g_panicSec`
  increments; stop the stress, verify it resets to 0.
- Remove the debug printf before merging (or gate behind `#if DEBUG`).

### Files changed

- `firmware/pc_tamagotchi/pc_tamagotchi.ino` (globals + tick block in `loop()`)

---

## Stage 2: Buzzer patterns (per-event melody signatures)

**Issue:** [#2](https://github.com/ichMaster/pc-pet/issues/2)
**Effort:** low
**Labels:** firmware
**Dependencies:** none (shared with stage 3)

### Summary

Replace the current hardcoded `M5.Speaker.tone()` calls with a reusable melody
system. Each event gets a distinct sound signature. This is device-only (no
protocol change) and feeds into panic tiers (stage 3).

### Implementation details

#### Melody data structure

```cpp
struct Note { uint16_t freq; uint16_t durMs; uint16_t pauseMs; };

// each melody is a small array of Notes terminated by {0,0,0}
const Note MEL_ALERT[] = {
  {2300, 90, 20}, {2300, 90, 0}, {0,0,0}
};
const Note MEL_PANIC1[] = {
  {2000, 100, 50}, {2400, 100, 0}, {0,0,0}
};
const Note MEL_PANIC2[] = {
  {2000, 80, 40}, {2400, 80, 40}, {2000, 80, 40}, {2400, 80, 0}, {0,0,0}
};
const Note MEL_PANIC3[] = {
  {2600, 60, 30}, {2200, 60, 30}, {2600, 60, 30}, {2200, 60, 30},
  {2800, 120, 0}, {0,0,0}
};
const Note MEL_LOWPWR[] = {
  {1200, 120, 20}, {900, 200, 0}, {0,0,0}
};
const Note MEL_LOWBATT[] = {
  {800, 150, 30}, {600, 200, 0}, {0,0,0}
};
const Note MEL_CLICK[] = {
  {1500, 30, 0}, {0,0,0}
};
const Note MEL_CHARSWITCH[] = {
  {1700, 30, 0}, {0,0,0}
};
const Note MEL_MUTE_ON[]  = { {600, 40, 0}, {0,0,0} };
const Note MEL_MUTE_OFF[] = { {1800, 40, 0}, {0,0,0} };
```

#### Play helper

Non-blocking playback is hard with `M5.Speaker.tone()`, but the current code
already uses blocking `delay()` between tones (line 812-813). Keep the same
pattern for now:

```cpp
void playMelody(const Note* mel) {
  if (g_mute || !mel) return;
  for (int i = 0; mel[i].freq != 0; i++) {
    M5.Speaker.tone(mel[i].freq, mel[i].durMs);
    delay(mel[i].durMs + mel[i].pauseMs);
  }
}
```

Place this in the helpers section (~line 310).

#### Replace existing tone calls

| Location | Current code | New code |
|---|---|---|
| BtnA click (line 770) | `M5.Speaker.tone(1500, 30)` | `playMelody(MEL_CLICK)` |
| BtnB single (line 775) | `M5.Speaker.tone(1700, 30)` | `playMelody(MEL_CHARSWITCH)` |
| BtnB double mute on (line 780) | `M5.Speaker.tone(600, 40)` | `playMelody(MEL_MUTE_ON)` |
| BtnB double mute off (line 780) | `M5.Speaker.tone(1800, 40)` | `playMelody(MEL_MUTE_OFF)` |
| Mood escalation (line 811-813) | two `tone(2300,90)` + delay | `playMelody(MEL_ALERT)` |
| Low battery (line 839-841) | `tone(1200,120)` + `tone(900,200)` | `playMelody(MEL_LOWBATT)` |

#### Melodies for panic tiers (used by stage 3)

Stage 3 will call `playMelody(MEL_PANIC1/2/3)` from the tier transition logic.
This issue defines the melodies; stage 3 wires them up.

#### Mute behavior

Keep global mute (the `g_mute` flag) -- `playMelody` checks it at the top.
Per-type muting is out of scope (roadmap open question, deferred).

### Testing

- Upload, verify each button still makes its sound.
- Trigger panic with `tools/cpu_stress.py --percent 90`, confirm the alert beep.
- Double-click BtnB to mute, confirm all sounds are suppressed.
- Manually test PANIC1/2/3 melodies by temporarily calling them from the mood
  escalation block.

### Files changed

- `firmware/pc_tamagotchi/pc_tamagotchi.ino`

---

## Stage 3: Panic tiers (3 escalating states by duration)

**Issue:** [#3](https://github.com/ichMaster/pc-pet/issues/3)
**Effort:** low-medium
**Labels:** firmware
**Dependencies:** stage 1 (tick), stage 2 (buzzer)

### Summary

The longer the PC stays in panic, the worse the pet looks and sounds. Three
tiers by continuous-panic duration, with instant revert when panic clears.

### Design

| Tier | Duration | Visual | Sound (from stage 2) |
|---|---|---|---|
| PANIC-1 | 0 -- T1 (10 s) | Current panic look (red body, wide eyes, sweat) | `MEL_PANIC1` on entry |
| PANIC-2 | T1 -- T2 (30 s) | Stronger distress: darker red, faster jitter, heavier sweat | `MEL_PANIC2` on transition |
| PANIC-3 | > T2 | Critical / "sick": desaturated, droopy eyes, slow pulse overlay | `MEL_PANIC3` on transition |

Instant revert: when `mood != M_PANIC`, `g_panicSec = 0` (already done by
stage 1 tick), tier drops to 0, visuals return to normal immediately.

### Implementation details

#### Constants and state

```cpp
const uint32_t PANIC_T1 = 10;   // seconds to reach tier 2
const uint32_t PANIC_T2 = 30;   // seconds to reach tier 3

// derived from g_panicSec (set by the tick in stage 1)
int panicTier(uint32_t sec) {
  if (sec >= PANIC_T2) return 3;
  if (sec >= PANIC_T1) return 2;
  return 1;
}
```

Place with the mood helpers (~line 320).

#### Track tier transitions for sound

In the 1 Hz tick block (added by stage 1), after `g_panicSec++`:

```cpp
if (mood == M_PANIC) {
  g_panicSec++;
  int tier = panicTier(g_panicSec);
  int prevTier = panicTier(g_panicSec - 1);
  if (tier != prevTier) {
    if (tier == 2) playMelody(MEL_PANIC2);
    if (tier == 3) playMelody(MEL_PANIC3);
  }
} else {
  g_panicSec = 0;
}
```

#### Visual changes in `drawPet()`

Pass the panic tier into `drawPet` (or compute it from a global). Modify the
existing PANIC rendering:

**Tier 2 (stronger distress):**
- `bodyColor(M_PANIC)` shifts darker: blend toward `color565(180, 50, 40)`
- Jitter amplitude: 2 -> 4 pixels
- Add a second sweat drop on each side

**Tier 3 (critical / sick):**
- Body color desaturated: blend toward `color565(140, 120, 130)`
- Eyes become half-closed (reuse `M_LOWPWR` eye style but with panic pupils)
- Mouth changes to a wavy/dizzy line
- Slow pulsing overlay: `fillEllipse` with alpha-blended red, sine-driven

#### Mood word

Extend `moodWord()` to show tier:
- Tier 1: `"PANIC!!"` (unchanged)
- Tier 2: `"PANIC!!!"` (extra `!`)
- Tier 3: `"CRITICAL"`

This requires passing the tier to the view or reading `g_panicSec` directly.

#### What NOT to do

- Do not persist `g_panicSec` across reboots (no NVS).
- Do not count `M_HOT` toward the panic timer (keep it simple for now; the
  roadmap lists this as an open question).
- Do not make T1/T2 configurable yet (stage 6 / phase 3 handles that).

### Testing

- `tools/cpu_stress.py --percent 90` -- hold for >30 s.
- Observe: tier 1 immediately, tier 2 at ~10 s (sound plays), tier 3 at ~30 s.
- Stop the stress tool -- pet should instantly return to its normal mood.
- Verify the 1 Hz serial heartbeat (or a temporary debug print) shows
  `panicSec` incrementing and resetting.

### Files changed

- `firmware/pc_tamagotchi/pc_tamagotchi.ino`

---

## Stage 4: Disk I/O metrics (read/write MB/s)

**Issue:** [#4](https://github.com/ichMaster/pc-pet/issues/4)
**Effort:** low
**Labels:** firmware, agent, protocol
**Dependencies:** none (independent of stages 1-3)

### Summary

Add disk read/write throughput to the metric stream. Two new fields in the
agent packet; a new row in the Stats screen on the device.

### Protocol change

#### Current metrics section (indices 0-8)

```
cpu,ram,temp,net,procs,topname,gpu,batt,charging
```

#### New metrics section (indices 0-10)

```
cpu,ram,temp,net,procs,topname,gpu,batt,charging,diskR,diskW
```

| idx | field | range / notes |
|---|---|---|
| 9 | diskR | disk read MB/s, integer (0-9999) |
| 10 | diskW | disk write MB/s, integer (0-9999) |

Worst-case packet growth: ~10 bytes (`",1234,5678"`). Current worst case is
~150 bytes; new worst case ~160 bytes, well within the 200-byte RX buffer and
185-byte MTU.

### Agent implementation (`agent/pc_pet_agent.py`)

#### New state in `Metrics.__init__`

```python
dio = psutil.disk_io_counters()
self._disk_r = dio.read_bytes if dio else 0
self._disk_w = dio.write_bytes if dio else 0
```

#### New logic in `Metrics.sample`

After the net throughput calculation:

```python
dio = psutil.disk_io_counters()
if dio:
    disk_r = int((dio.read_bytes - self._disk_r) / dt / 1_048_576)  # MB/s
    disk_w = int((dio.write_bytes - self._disk_w) / dt / 1_048_576)
    self._disk_r = dio.read_bytes
    self._disk_w = dio.write_bytes
else:
    disk_r, disk_w = 0, 0
disk_r = max(0, min(disk_r, 9999))
disk_w = max(0, min(disk_w, 9999))
```

#### Updated packet format string

```python
return (f"{cpu},{ram},{temp},{net_kbs},{procs},{busiest},"
        f"{gpu},{batt},{charging},{disk_r},{disk_w};{cpu_list};{ram_list}")
```

### Firmware implementation (`firmware/pc_tamagotchi/pc_tamagotchi.ino`)

#### New globals

```cpp
volatile int g_diskR = 0, g_diskW = 0;   // disk read/write MB/s
```

#### Parse in `parsePacket()`

Add cases to the metrics switch (~line 189):

```cpp
case 9:  diskR = atoi(tok); break;
case 10: diskW = atoi(tok); break;
```

And in the critical section:

```cpp
g_diskR = constrain(diskR, 0, 9999);
g_diskW = constrain(diskW, 0, 9999);
```

#### Snapshot in `loop()`

Add `diskR`/`diskW` to the local snapshot block (~line 798).

#### Stats screen (`viewStats`)

Add a new row after the battery line:

```cpp
char ds[28];
snprintf(ds, sizeof(ds), "disk: R%d W%d MB/s", diskR, diskW);
canvas.drawString(ds, bx, y);  y += 14;
```

#### Backward compatibility

The firmware parser uses a positional index with a `while(tok)` loop -- if
indices 9 and 10 are absent (old agent), `diskR`/`diskW` stay at their
default (0). Old agent + new firmware works fine. New agent + old firmware also
works: the firmware ignores unknown trailing fields.

### Documentation updates

- `docs/protocol.md`: add `diskR` (idx 9) and `diskW` (idx 10) to the table.
- `README.md`: mention disk I/O in the Stats screen description.

### Testing

- Run the updated agent, check `[send]` output includes two extra fields.
- On the device Stats screen, verify "disk: R__ W__ MB/s" appears.
- Copy a large file to trigger visible read/write numbers.
- Test with the OLD agent (no disk fields) -- firmware should show 0/0, no crash.

### Files changed

- `agent/pc_pet_agent.py` (Metrics class)
- `firmware/pc_tamagotchi/pc_tamagotchi.ino` (globals, parser, snapshot, Stats view)
- `docs/protocol.md` (table update)
- `README.md` (Stats screen description)
