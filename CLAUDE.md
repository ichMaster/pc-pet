# CLAUDE.md — working notes for Claude Code

Guidance for working on this repository. Read `README.md` for setup,
`docs/protocol.md` for the wire format, and `ROADMAP.md` for the planned work.

## What this project is

A two-part system: Arduino C++ firmware on an M5StickC Plus2 (BLE peripheral)
and a Python host agent on the PC (BLE central). The PC streams system metrics;
the device renders an animated "pet" whose mood reflects those metrics, plus
info screens.

## Hard rules

- **No emoji** anywhere — code, comments, UI text, commit messages, docs.
- **Mood enum values are `M_`-prefixed** (`M_SLEEP`, `M_HAPPY`, `M_BUSY`,
  `M_STUFFED`, `M_HOT`, `M_PANIC`, `M_LOWPWR`). The ESP32 ROM headers already
  define `BUSY`/`HOT` in their own enum, so unprefixed names will not compile.
- **Keep the BLE write callback tiny.** It runs on the Bluetooth task, which has
  a small stack. The callback only stashes raw bytes + sets a flag; all parsing
  and printing happen in `loop()` on the main task. Do not add buffers, parsing,
  or `Serial.printf` inside the callback (it caused stack-overflow resets).
- **RX buffer is 200 bytes**; keep packets comfortably under the negotiated MTU.
- Use Write Request (`response=True`) from the agent — some ESP32 stacks fire
  `onWrite` reliably only for Write Requests, not Write Commands.

## Firmware conventions (`firmware/pc_tamagotchi/pc_tamagotchi.ino`)

- The sketch folder name must match the `.ino` name for the Arduino IDE.
- Globals shared between `setup()`/`loop()` must be declared above `setup()`
  (C++ visibility) — a previous bug came from declaring power globals after it.
- Shared state between BLE callback and loop is guarded with `g_mux`
  (portMUX critical sections). Proc-list arrays are written and read on the main
  task only.
- Rendering goes through an offscreen `M5Canvas` then `pushSprite`.
- Screen power: dim after `DIM_AFTER_MS`, off after `OFF_AFTER_MS`; wakes on
  button, sustained strong shake (`SHAKE_THRESH` / `SHAKE_HOLD_MS`), or an alert
  mood. Power-button short click toggles the screen; hold ~1 s powers off.
- Mood is computed on-device in `currentMood(cpu, ram, temp, gpu, batt, charging)`.
- Characters: `drawCharBody()` switches on `g_char`; eyes/mouth/effects are shared
  across characters and driven by mood.
- Serial debug prints (`RX(...)`, 1 Hz `dbg:` heartbeat) are currently always on;
  consider gating them behind a `DEBUG` macro before shipping.

## Agent conventions (`agent/pc_pet_agent.py`)

- `Metrics.sample()` builds the packet (see `docs/protocol.md`).
- `MacMon` runs `macmon raw` in a background thread (Apple Silicon, sudoless) for
  CPU temp + GPU load so the asyncio BLE loop never blocks. `-s 1` takes one
  sample; if a macmon version lacks that flag, check `macmon raw --help`.
- Battery comes from `psutil.sensors_battery()`.
- `find_device()` matches by name substring OR the NUS service UUID, and supports
  an explicit `--address`.

## Build / run

See `README.md`. Phases 1-2 use the Arduino IDE; from Phase 3 onward the
firmware builds with PlatformIO (`pio run -t upload`). Agent: `pip install -r
agent/requirements.txt`, then `python3 agent/pc_pet_agent.py`.

## How to extend (next features)

`ROADMAP.md` defines the selected work and the recommended order (#5 and #8 are
each split into two tasks). In short:

1. **1.1 tick:** a 1 Hz `millis()`-based counter; foundation for panic tiers.
2. **8a buzzer patterns (device-only):** a "play melody" helper + a per-event
   sound signature (PANIC-1/2/3, low-power, stick low-battery). No protocol
   change. Build before/with #2.
3. **#2 panic tiers** (replaces the old "health" idea): three escalating states by
   continuous-panic duration (`p1`/`p2`), instant revert when panic clears. No
   persistence. Thresholds hardcoded until 8b.
4. **#6 disk I/O:** two extra metric fields (read/write MB/s) + a Stats line.
5. **#7 ENV III:** read **SHT30 (0x44) + QMP6988 (0x70)** over `Wire1.begin(0, 26)`
   using the M5Unit-ENV library (ENV III path, not BMP280). Add an ENV screen and
   an optional mood modifier. Autonomous from the PC link; presence-check on init.
6. **8b threshold config channel:** a `CFG;`-prefixed line (agent -> device over
   the existing RX char) carrying thresholds; the device branches on the first
   token (`CFG` vs a number). Cross-cutting (agent + device) — higher complexity.
7. **5a reverse channel — safe commands:** device -> PC over TX notify, from a
   fixed whitelist (lock / play-pause / volume / run-script). A Remote screen in
   the BtnA cycle; long-press to execute.
8. **5b Kill (low priority, after #8):** isolated irreversible task — kill top /
   kill selected (from Procs), always with on-screen confirmation. Scheduled last.

## Things deliberately NOT done (and why)

- **NVS / persistence:** not needed for the selected features — panic tiers reset
  instantly, and config (8b) is re-sent by the agent on each connect. Consequence:
  character and mute selection do not survive a reboot. Add `Preferences` (NVS)
  only if persistence becomes desired.
- **Achievements / petting interaction:** out of current scope.
