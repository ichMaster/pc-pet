# Phase 1 -- Core mechanics

Foundation + first real gameplay features. All device-side except disk I/O.

## Issues Summary Table

| # | ID | Title | Size | Stage | Dependencies |
|---|---|---|---|---|---|
| 1 | PCP-001 | One-second tick | S | 1 -- Foundation | -- |
| 2 | PCP-002 | Buzzer patterns | S | 2 -- Audio | -- |
| 3 | PCP-003 | Panic tiers | M | 3 -- Panic | PCP-001, PCP-002 |
| 4 | PCP-004 | Disk I/O metrics | S | 4 -- Metrics | -- |

**Size legend:** S = 1-2 days, M = 3-5 days, L = 5-8 days

---

## Dependency Tree

```
    PCP-001 (tick)     PCP-002 (buzzer)     PCP-004 (disk I/O)
        |                   |
        +-------+-----------+
                |
            PCP-003
          (panic tiers)
```

**Parallelization hints:**

- PCP-001 and PCP-002 can run in parallel (no shared code)
- PCP-004 is fully independent, can run in parallel with everything
- PCP-003 must wait for both PCP-001 and PCP-002

---

## Stage 1 -- Foundation

### PCP-001 -- One-second tick (1 Hz millis-based counter)

**Description:**
Add a stable 1 Hz time base to the firmware. The current frame rate varies
(~18 fps / 55 ms when the screen is on, ~6.7 fps / 150 ms when off), so
frame-counting is unreliable for measuring real-world durations. This tick is
the foundation for panic tiers (PCP-003).

**What needs to be done:**
- Declare new globals above `setup()` (per project convention):
  ```cpp
  uint32_t g_lastTick1s = 0;   // last time the 1 s tick fired
  uint32_t g_uptimeSec  = 0;   // seconds since boot (informational)
  uint32_t g_panicSec   = 0;   // continuous seconds in M_PANIC (used by PCP-003)
  ```
- Insert a 1 Hz tick block in `loop()`, right after the mood computation (~line
  808), before screen power management:
  ```cpp
  if (millis() - g_lastTick1s >= 1000) {
    g_lastTick1s = millis();
    g_uptimeSec++;
    if (mood == M_PANIC)
      g_panicSec++;
    else
      g_panicSec = 0;
  }
  ```
- Use `millis()` delta, not `delay()` -- non-blocking
- `g_panicSec` resets instantly when panic clears (instant revert, per roadmap)
- Placement after mood computation ensures the tick sees the current mood
- No mutex needed -- all reads/writes are on the main task
- Do NOT use `g_frame` for duration measurement (it drifts with screen state)
- Do NOT add FreeRTOS timers or separate tasks -- keep everything on `loop()`
- Do NOT persist `g_uptimeSec` (no NVS in scope)

**Dependencies:** None

**Expected result:**
A stable 1 Hz counter that increments `g_panicSec` while in panic mood and
resets it instantly when panic clears, regardless of screen on/off state.

**Acceptance criteria:**
- [ ] Tick fires once per second (verified via temporary `Serial.printf`)
- [ ] Tick rate is consistent whether screen is on (55 ms loop) or off (150 ms loop)
- [ ] `g_panicSec` increments during `M_PANIC` mood (test with `cpu_stress.py --percent 90`)
- [ ] `g_panicSec` resets to 0 immediately when panic clears
- [ ] No new mutex or task introduced
- [ ] Only `firmware/pc_tamagotchi/pc_tamagotchi.ino` changed

---

## Stage 2 -- Audio

### PCP-002 -- Buzzer patterns (per-event melody signatures)

**Description:**
Replace the current hardcoded `M5.Speaker.tone()` calls with a reusable melody
system. Each event gets a distinct sound signature. This is device-only (no
protocol change) and provides melodies that PCP-003 (panic tiers) will use.

**What needs to be done:**
- Define a `Note` struct and melody arrays:
  ```cpp
  struct Note { uint16_t freq; uint16_t durMs; uint16_t pauseMs; };

  const Note MEL_ALERT[]      = { {2300, 90, 20}, {2300, 90, 0}, {0,0,0} };
  const Note MEL_PANIC1[]     = { {2000, 100, 50}, {2400, 100, 0}, {0,0,0} };
  const Note MEL_PANIC2[]     = { {2000, 80, 40}, {2400, 80, 40}, {2000, 80, 40}, {2400, 80, 0}, {0,0,0} };
  const Note MEL_PANIC3[]     = { {2600, 60, 30}, {2200, 60, 30}, {2600, 60, 30}, {2200, 60, 30}, {2800, 120, 0}, {0,0,0} };
  const Note MEL_LOWPWR[]     = { {1200, 120, 20}, {900, 200, 0}, {0,0,0} };
  const Note MEL_LOWBATT[]    = { {800, 150, 30}, {600, 200, 0}, {0,0,0} };
  const Note MEL_CLICK[]      = { {1500, 30, 0}, {0,0,0} };
  const Note MEL_CHARSWITCH[] = { {1700, 30, 0}, {0,0,0} };
  const Note MEL_MUTE_ON[]    = { {600, 40, 0}, {0,0,0} };
  const Note MEL_MUTE_OFF[]   = { {1800, 40, 0}, {0,0,0} };
  ```
- Implement `playMelody` helper in the helpers section (~line 310):
  ```cpp
  void playMelody(const Note* mel) {
    if (g_mute || !mel) return;
    for (int i = 0; mel[i].freq != 0; i++) {
      M5.Speaker.tone(mel[i].freq, mel[i].durMs);
      delay(mel[i].durMs + mel[i].pauseMs);
    }
  }
  ```
- Replace all existing `M5.Speaker.tone()` call sites:

  | Location | Current code | New code |
  |---|---|---|
  | BtnA click (line 770) | `M5.Speaker.tone(1500, 30)` | `playMelody(MEL_CLICK)` |
  | BtnB single (line 775) | `M5.Speaker.tone(1700, 30)` | `playMelody(MEL_CHARSWITCH)` |
  | BtnB double mute on (line 780) | `M5.Speaker.tone(600, 40)` | `playMelody(MEL_MUTE_ON)` |
  | BtnB double mute off (line 780) | `M5.Speaker.tone(1800, 40)` | `playMelody(MEL_MUTE_OFF)` |
  | Mood escalation (line 811-813) | two `tone(2300,90)` + delay | `playMelody(MEL_ALERT)` |
  | Low battery (line 839-841) | `tone(1200,120)` + `tone(900,200)` | `playMelody(MEL_LOWBATT)` |

- Keep global mute (`g_mute` flag) -- `playMelody` checks it at the top
- Per-type muting is out of scope (roadmap open question, deferred)
- PCP-003 will wire up `MEL_PANIC1/2/3`; this issue only defines them

**Dependencies:** None

**Expected result:**
All existing sounds are driven through `playMelody`. New melodies for panic
tiers and low-power events are defined and ready for use by PCP-003.

**Acceptance criteria:**
- [ ] No direct `M5.Speaker.tone()` calls remain in `loop()` (all go through `playMelody`)
- [ ] BtnA click, BtnB single click, BtnB double click all produce their expected sound
- [ ] Mood escalation alert beep still plays on panic entry
- [ ] Low-battery beep still plays when stick battery drops below 10%
- [ ] Double-click BtnB (mute) suppresses all sounds
- [ ] `MEL_PANIC1`, `MEL_PANIC2`, `MEL_PANIC3` arrays compile (tested by temporary call)
- [ ] Only `firmware/pc_tamagotchi/pc_tamagotchi.ino` changed

---

## Stage 3 -- Panic

### PCP-003 -- Panic tiers (3 escalating states by duration)

**Description:**
The longer the PC stays in panic, the worse the pet looks and sounds. Three
tiers by continuous-panic duration, with instant revert when panic clears. Uses
the tick from PCP-001 and the melodies from PCP-002.

**What needs to be done:**
- Define tier constants and helper with the mood helpers (~line 320):
  ```cpp
  const uint32_t PANIC_T1 = 10;   // seconds to reach tier 2
  const uint32_t PANIC_T2 = 30;   // seconds to reach tier 3

  int panicTier(uint32_t sec) {
    if (sec >= PANIC_T2) return 3;
    if (sec >= PANIC_T1) return 2;
    return 1;
  }
  ```
- Extend the 1 Hz tick block (from PCP-001) to detect tier transitions and play
  sounds:
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
- Modify `drawPet()` to vary rendering by panic tier:
  - **Tier 1 (0-10 s):** current panic look (red body, wide eyes, sweat) -- no change
  - **Tier 2 (10-30 s):** darker red body (blend toward `color565(180, 50, 40)`),
    jitter amplitude 2 -> 4 pixels, second sweat drop on each side
  - **Tier 3 (>30 s):** desaturated body (blend toward `color565(140, 120, 130)`),
    half-closed eyes (reuse `M_LOWPWR` eye style with panic pupils), wavy/dizzy
    mouth, slow pulsing red overlay (sine-driven `fillEllipse`)
- Extend `moodWord()` to show tier:
  - Tier 1: `"PANIC!!"` (unchanged)
  - Tier 2: `"PANIC!!!"` (extra `!`)
  - Tier 3: `"CRITICAL"`
- Do NOT persist `g_panicSec` across reboots (no NVS)
- Do NOT count `M_HOT` toward the panic timer (open question, deferred)
- Do NOT make T1/T2 configurable yet (phase 3 / PCP-008 handles that)

**Dependencies:** PCP-001, PCP-002

**Expected result:**
Panic mood visually and audibly escalates through 3 tiers based on continuous
duration, and instantly reverts to normal when panic clears.

**Acceptance criteria:**
- [ ] Tier 1 renders immediately when panic starts (existing look, no regression)
- [ ] Tier 2 triggers at ~10 s with `MEL_PANIC2` sound and visual change
- [ ] Tier 3 triggers at ~30 s with `MEL_PANIC3` sound and visual change
- [ ] Stopping `cpu_stress.py` instantly reverts pet to its normal mood (no lingering)
- [ ] `moodWord()` returns `"PANIC!!"`, `"PANIC!!!"`, or `"CRITICAL"` per tier
- [ ] Serial debug (or temporary print) shows `panicSec` incrementing and resetting
- [ ] Only `firmware/pc_tamagotchi/pc_tamagotchi.ino` changed

---

## Stage 4 -- Metrics

### PCP-004 -- Disk I/O metrics (read/write MB/s)

**Description:**
Add disk read/write throughput to the metric stream. Two new fields in the
agent packet; a new row in the Stats screen on the device.

**What needs to be done:**
- **Protocol change** -- extend the metrics section with two new fields:

  | idx | field | range / notes |
  |---|---|---|
  | 9 | diskR | disk read MB/s, integer (0-9999) |
  | 10 | diskW | disk write MB/s, integer (0-9999) |

  Packet format becomes:
  `cpu,ram,temp,net,procs,topname,gpu,batt,charging,diskR,diskW;cpuList;ramList`

  Worst-case growth: ~10 bytes. New worst case ~160 bytes, within the 200-byte
  RX buffer and 185-byte MTU.

- **Agent** (`agent/pc_pet_agent.py`):
  - Add disk state to `Metrics.__init__`:
    ```python
    dio = psutil.disk_io_counters()
    self._disk_r = dio.read_bytes if dio else 0
    self._disk_w = dio.write_bytes if dio else 0
    ```
  - Add disk throughput calculation to `Metrics.sample` (after net calculation):
    ```python
    dio = psutil.disk_io_counters()
    if dio:
        disk_r = int((dio.read_bytes - self._disk_r) / dt / 1_048_576)
        disk_w = int((dio.write_bytes - self._disk_w) / dt / 1_048_576)
        self._disk_r = dio.read_bytes
        self._disk_w = dio.write_bytes
    else:
        disk_r, disk_w = 0, 0
    disk_r = max(0, min(disk_r, 9999))
    disk_w = max(0, min(disk_w, 9999))
    ```
  - Append `{disk_r},{disk_w}` to the packet format string

- **Firmware** (`firmware/pc_tamagotchi/pc_tamagotchi.ino`):
  - Add globals: `volatile int g_diskR = 0, g_diskW = 0;`
  - Add parse cases in `parsePacket()` metrics switch:
    ```cpp
    case 9:  diskR = atoi(tok); break;
    case 10: diskW = atoi(tok); break;
    ```
  - Add to critical section: `g_diskR = constrain(diskR, 0, 9999); g_diskW = constrain(diskW, 0, 9999);`
  - Add to local snapshot block in `loop()` (~line 798)
  - Add Stats screen row after battery line:
    ```cpp
    char ds[28];
    snprintf(ds, sizeof(ds), "disk: R%d W%d MB/s", diskR, diskW);
    canvas.drawString(ds, bx, y);  y += 14;
    ```

- **Backward compatibility:** old agent (no disk fields) -> firmware shows 0/0,
  no crash. New agent + old firmware -> firmware ignores unknown trailing fields.

- **Documentation:** update `docs/protocol.md` table and `README.md` Stats
  screen description.

**Dependencies:** None

**Expected result:**
Disk read/write throughput appears in the agent packet and on the device Stats
screen. Both old-agent and old-firmware combinations work without breakage.

**Acceptance criteria:**
- [ ] Agent `[send]` output includes two extra comma-separated fields after `charging`
- [ ] Stats screen shows "disk: R__ W__ MB/s" with live values
- [ ] Copying a large file produces visible non-zero read/write numbers
- [ ] Old agent (no disk fields) + new firmware shows 0/0, no crash
- [ ] New agent + old firmware works (firmware ignores extra fields)
- [ ] `docs/protocol.md` updated with `diskR` (idx 9) and `diskW` (idx 10)
- [ ] Files changed: `agent/pc_pet_agent.py`, `firmware/pc_tamagotchi/pc_tamagotchi.ino`, `docs/protocol.md`, `README.md`
