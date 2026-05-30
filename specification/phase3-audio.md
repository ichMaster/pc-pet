# Phase 3 -- SPK2 audio HAT

Richer audio via the M5Stack Hat SPK2 (MAX98357 I2S Class-D amp + 1 W speaker)
instead of the built-in passive buzzer. Full feature spec:
`specification/spk2-audio-features.md`; roadmap stage table: ROADMAP Phase 3.

**Hard constraint -- one HAT at a time.** SPK2 (I2S on G0/G25/G26) and the
Phase 2 ENV III HAT (I2C on G0/G26) share the single top HAT port and collide on
pins. A session runs **either** ENV III **or** SPK2, never both. The firmware
must decide which at boot (see PCP-012 for the detection caveat -- MAX98357 has
no I2C address, so detection is by ENV absence, not SPK2 presence).

**Design principle.** SPK2 is just another output. The existing sound system
(`playMelody()`, `MEL_*` arrays, `g_mute`) keeps its logic; SPK only changes
*where* audio renders (I2S amp vs. built-in `M5.Speaker`). Everything below is
additive on top of SPK-1 routing.

**Build:** Phase 3 stays on the Arduino IDE (the PlatformIO migration is
Phase 4). All issues are device-only -- no agent or protocol changes.

## Issues Summary Table

| # | ID | Title | Size | Stage | Dependencies |
|---|---|---|---|---|---|
| 1 | PCP-012 | SPK2 audio routing + HAT mode select | M | 1 -- Routing | -- |
| 2 | PCP-013 | UI sound feedback + panic siren | M | 2 -- Cues | PCP-012 |
| 3 | PCP-014 | Mood ambient loops | M | 3 -- Ambient | PCP-012 |
| 4 | PCP-015 | WAV/PCM notifications from flash | L | 4 -- Voice | PCP-012 |
| 5 | PCP-016 | Heartbeat + reactive chirps | M | 5 -- Polish | PCP-012 |

**Size legend:** S = 1-2 days, M = 3-5 days, L = 5-8 days

---

## Dependency Tree

```
                 PCP-012 (routing + HAT mode select)
                     |
        +------------+------------+------------+
        |            |            |            |
    PCP-013      PCP-014      PCP-015      PCP-016
    (cues)       (ambient)    (voice)      (polish)
```

**Parallelization hints:**

- PCP-012 must land first -- it provides the I2S output path everything reuses.
- PCP-013/014/015/016 are independent of each other and can run in parallel once
  PCP-012 lands.
- Recommended order for demo impact (per the SPK spec): 012 -> 013 -> 014 ->
  015 -> 016.

---

## Stage 1 -- Routing

### PCP-012 -- SPK2 audio routing + HAT mode select (SPK-1)

**Description:**
Route the device's audio to the SPK2 I2S amplifier when SPK2 mode is active;
otherwise fall back to the built-in `M5.Speaker`. This is the foundation -- every
other SPK feature reuses this one output path. It also establishes the ENV-vs-SPK
HAT mode selection, since the two HATs are mutually exclusive on the same port.

**What needs to be done:**
- **HAT pins (SPK2):** DATA/DOUT = G25, BCLK = G26, LRC/WS = G0
  (MAX98357 I2S; no I2C address).
- **Mode selection.** MAX98357 cannot be probed on I2C, so "auto-detect SPK2"
  is not literally possible. Use ENV absence + an explicit opt-in:
  - At boot, run the existing ENV III presence check (I2C 0x44/0x70 on
    `Wire(0,26)`, from PCP-005). If ENV is present -> `HAT_ENV` mode, audio stays
    on the built-in speaker.
  - If ENV is absent -> allow `HAT_SPK2` mode. Because absence does not prove an
    SPK2 is fitted, gate the actual I2S routing behind a **persisted opt-in** so
    a bare unit (no HAT) is not forced onto dead I2S pins. Options, pick one and
    document it: (a) a compile-time `#define USE_SPK2`, or (b) a boot button-hold
    (e.g. hold BtnB at power-on) latched for the session, or (c) a `Preferences`
    (NVS) flag toggled from a settings screen. Recommended for Phase 3: **(a)
    compile-time define** (simplest; no NVS, matches "no persistence" scope),
    with (b)/(c) noted as future work.
  - Expose the result as a global, e.g. `enum HatMode { HAT_NONE, HAT_ENV,
    HAT_SPK2 }; HatMode g_hat;`.
- **Route audio.** When `g_hat == HAT_SPK2`, reconfigure `M5.Speaker` for the
  HAT's I2S pins before first use, e.g. via `M5.Speaker.config()` setting
  `pin_data_out = 25`, `pin_bck = 26`, `pin_ws = 0` (verify exact field names
  against the installed M5Unified `Speaker_Class` config struct -- API differs by
  version). Otherwise leave the default built-in speaker config untouched.
- `playMelody()` and all `MEL_*` signatures must work unchanged through the new
  route -- no call-site changes. `g_mute` still respected.
- Boot serial line reporting the chosen mode, e.g.
  `Serial.printf("HAT: %s\n", ...)`.
- Do NOT break ENV III: when `HAT_ENV`, behave exactly as Phase 2.
- Do NOT add new melodies here -- routing only (later issues add content).
- Do NOT assume MAX98357 is I2C-detectable.

**Dependencies:** None (uses the existing ENV presence check from PCP-005).

**Expected result:**
On a unit with SPK2 (opt-in active, ENV absent), all existing sounds play through
the I2S amplifier -- louder and clearer. On an ENV III unit, audio stays on the
built-in speaker and ENV works as before. On a bare unit, audio stays on the
built-in speaker.

**Acceptance criteria:**
- [ ] `HatMode` global set at boot: ENV present -> HAT_ENV; ENV absent + opt-in -> HAT_SPK2; else HAT_NONE
- [ ] Boot serial line reports the selected HAT mode
- [ ] In HAT_SPK2, existing button/alert/low-battery sounds play via I2S (G25/G26/G0)
- [ ] In HAT_ENV, ENV III still works and audio uses the built-in speaker (no regression)
- [ ] `playMelody()` / `MEL_*` call sites unchanged; `g_mute` still mutes
- [ ] Mode-select mechanism (define / button / NVS) documented in a code comment
- [ ] Only `firmware/pc_tamagotchi/*` changed

---

## Stage 2 -- Cues

### PCP-013 -- UI sound feedback + panic siren (SPK-5 + SPK-3)

**Description:**
Two high-impact, demo-friendly audio features that ride PCP-012 routing:
distinct UI cues for on-device interaction (SPK-5), and a panic siren that
escalates with the existing #2 panic tiers (SPK-3). Both are device-only and
reuse `playMelody()`.

**What needs to be done:**
- **UI feedback (SPK-5):** richer, distinct cues for screen switch (BtnA),
  character change (BtnB single), mute toggle (BtnB double -- a short confirming
  blip even as it mutes the rest), and screen wake. The current code already maps
  `MEL_CLICK` / `MEL_CHARSWITCH` / `MEL_MUTE_OFF`; refine them for the amp (they
  can be subtler/cleaner than buzzer beeps) and add a wake cue. Keep them short,
  never fatiguing.
- **Panic siren (SPK-3):** replace the one-shot tier melodies with a *sustained*
  siren that scales with `panicTier(g_panicSec)`:
  - PANIC-1: short repeating chirp.
  - PANIC-2: two-tone alarm, faster cadence.
  - PANIC-3: continuous rising/falling siren until panic clears.
  - **Instant silence** the moment `mood != M_PANIC` (matches #2 instant revert).
- **Non-blocking caveat:** the current `playMelody()` is blocking (`delay()` per
  note), which freezes rendering. A sustained siren cannot block the loop. Add a
  non-blocking tone scheduler (advance one note per `loop()` based on `millis()`)
  for the siren, or use `M5.Speaker`'s async playback. Document the approach.
- Respect `g_mute` everywhere.
- Do NOT block `loop()` for the duration of the siren.

**Dependencies:** PCP-012

**Expected result:**
Button actions have crisp distinct cues through the amp; a sustained PC panic
produces an escalating siren that scales with the tier and cuts off instantly
when panic clears, all without freezing the animation.

**Acceptance criteria:**
- [ ] BtnA / BtnB-single / BtnB-double / wake each have a distinct cue
- [ ] Panic siren escalates PANIC-1 -> 2 -> 3 with the existing tiers
- [ ] Siren stops within one loop of panic clearing (instant revert)
- [ ] Siren playback is non-blocking (animation keeps running)
- [ ] `g_mute` silences all of it
- [ ] Only `firmware/pc_tamagotchi/*` changed

---

## Stage 3 -- Ambient

### PCP-014 -- Mood ambient loops (SPK-2)

**Description:**
Give each mood a continuous, low-volume ambient sound so the creature feels
alive, not just animated ("the pet has a voice"). Only meaningful with the amp
(too harsh on the buzzer), so gate on `HAT_SPK2`.

**What needs to be done:**
- A low-volume looping ambient per mood:
  - HAPPY: soft purr / gentle hum.
  - SLEEP: slow breathing / light snore, synced to the Zzz animation.
  - BUSY: a quiet working "blip-blip" rhythm.
  - STUFFED: occasional content sigh.
  - LOWPWR: faint, slowing whimper as battery drops.
- Loops are quiet by default and **fade in/out on mood change** (no clicks).
- Ambient must yield to alerts/siren (PCP-013) and UI cues -- they take priority.
- Non-blocking, driven from `loop()` -- never freeze rendering.
- Respect `g_mute`.
- Only active in `HAT_SPK2` mode.
- **Open question (from SPK spec):** synthesized tones vs. short looped WAV
  samples; default ambient volume relative to alerts. Recommend synthesized for
  Phase 3 (no flash assets); WAV ambient can come with PCP-015.

**Dependencies:** PCP-012

**Expected result:**
While idle on the Pet screen, the creature emits a quiet mood-appropriate ambient
that fades smoothly on mood changes and never steps on alerts or UI cues.

**Acceptance criteria:**
- [ ] Each of HAPPY / SLEEP / BUSY / STUFFED / LOWPWR has a distinct ambient
- [ ] Ambient fades in/out on mood change without clicks
- [ ] Alerts, siren, and UI cues take priority over ambient
- [ ] Non-blocking; animation unaffected
- [ ] `g_mute` silences ambient; only active in HAT_SPK2
- [ ] Only `firmware/pc_tamagotchi/*` changed

---

## Stage 4 -- Voice

### PCP-015 -- WAV/PCM notifications from flash (SPK-4)

**Description:**
Play short recorded clips for key events -- only practical with a real amplifier.
Stores small WAV/PCM assets in flash and plays them on events.

**What needs to be done:**
- Store small WAV/PCM assets in flash (SPIFFS / LittleFS, or `const` PROGMEM
  arrays for the smallest clips). Decide the storage mechanism and document it.
- Play on events: boot chime, "overheating" (enter M_HOT), "low power"
  (enter M_LOWPWR), "back online" (BLE link recovers after a drop).
- Clips are short (well under a second up to a few seconds) to fit flash; define
  a flash budget.
- Playback via `M5.Speaker.playRaw()` (or `playWav()` if the M5Unified build
  exposes it) on the I2S route; non-blocking.
- Respect `g_mute`; only active in `HAT_SPK2`.
- **Open questions (from SPK spec):** asset format/size budget; which events get
  a voice clip vs. a tone. Resolve and record in the issue before building.
- Do NOT bloat the sketch with large arrays -- prefer a filesystem if assets grow.

**Dependencies:** PCP-012

**Expected result:**
Key events play short recorded clips through the amp (boot chime, overheating,
low power, back online), within a defined flash budget, without blocking.

**Acceptance criteria:**
- [ ] Flash storage mechanism chosen + documented (SPIFFS/LittleFS or PROGMEM)
- [ ] Boot chime + at least overheating / low-power / back-online clips play
- [ ] Clips play via the I2S route, non-blocking
- [ ] Flash budget defined and respected
- [ ] `g_mute` silences clips; only active in HAT_SPK2
- [ ] Only `firmware/pc_tamagotchi/*` (+ any assets dir) changed

---

## Stage 5 -- Polish

### PCP-016 -- Heartbeat + reactive chirps (SPK-7 + SPK-8)

**Description:**
Two incremental "alive" touches: an optional soft heartbeat whose rate tracks CPU
load (SPK-7), and playful R2-D2-style chirps on sharp metric changes (SPK-8).
Both off-by-default-friendly and built on the existing 1.1 tick + CPU history.

**What needs to be done:**
- **Heartbeat (SPK-7):** a very soft periodic "tick" whose rate follows CPU load
  (calm when idle, quicker when busy). Uses the existing 1 Hz tick (`g_lastTick1s`
  / `g_uptimeSec`) for timing. Off by default; toggleable.
  - Open question (SPK spec): tie tick rate to CPU, to mood, or keep constant.
    Recommend CPU.
- **Reactive chirps (SPK-8):** on a fast spike/drop in CPU/RAM/GPU/temperature,
  emit a short generated chirp whose pitch/length maps to the size + direction of
  the change. Use the existing CPU history ring (`g_hist`) and recent metric
  deltas; extend with small previous-value trackers for RAM/GPU/temp as needed.
  - **Rate-limit** (cooldown window) so it stays charming, not noisy.
  - Open question (SPK spec): per-metric chirp signatures; cooldown length.
- Non-blocking; respect `g_mute`; HAT_SPK2 only (heartbeat/chirps are amp-only
  polish).

**Dependencies:** PCP-012

**Expected result:**
An optional heartbeat that quickens under load, plus rate-limited chirps that
react to sharp metric changes -- both subtle, non-blocking, and muteable.

**Acceptance criteria:**
- [ ] Heartbeat rate tracks CPU load; off by default, toggleable
- [ ] Sharp metric change produces a chirp mapped to size/direction
- [ ] Chirps are rate-limited (cooldown) so they stay sparse
- [ ] Non-blocking; `g_mute` silences both; HAT_SPK2 only
- [ ] Only `firmware/pc_tamagotchi/*` changed

---

## Deferred (not in Phase 3)

- **SPK-6 agent-event sounds** (disk #6 / remote 5a / kill 5b) -- land
  incrementally with those features in later phases.
- **SPK-9 volume policy / quiet hours** -- depends on the #8b config channel
  (Phase 4), so it follows the config work.

## Cross-cutting open questions

1. **HAT mode select** -- compile-time define vs. boot button-hold vs. NVS flag
   (MAX98357 is not I2C-detectable). PCP-012 recommends a compile-time define for
   Phase 3.
2. **Non-blocking audio** -- `playMelody()` is currently blocking; the siren and
   ambient need a non-blocking scheduler. Decide once in PCP-013 and reuse.
3. **Synthesized vs. WAV** for ambient/voice (PCP-014 / PCP-015); flash budget
   for samples.
4. **M5Unified Speaker I2S config** -- exact `config()` field names vary by
   library version; verify on-device during PCP-012.
