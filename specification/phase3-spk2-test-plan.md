# Phase 3 -- SPK2 manual test plan

Manual on-device tests for the SPK2 audio HAT (PCP-012..016). The firmware has
no automated test harness, so these are run by hand with the HAT fitted.

## Preconditions

1. **Hardware:** M5StickC Plus2 with the **SPK2 HAT** fitted (ENV III removed --
   they share the port).
2. **Firmware:** `firmware/pc_tamagotchi/pet_config.h` has
   `#define HAT_SELECT HAT_SPK2`; sketch compiled and uploaded from the Arduino
   IDE (board M5StickCPlus2).
3. **Serial monitor** open at 115200 baud.
4. For the metric-driven tests (siren, ambient, overheat/low-power voice, chirp),
   the **PC agent must be running and connected** -- SPK2 mode changes only the
   audio route, BLE still works. Use `tools/cpu_stress.py` to drive CPU.
5. Start each test **unmuted** (mute = BtnB double-click; "mute" shows in the top
   bar). `g_heartbeatOn` is false by default.

Legend: **[dev]** = device-only, no PC needed. **[pc]** = needs the agent.

---

## T0 -- Boot routing + chime (PCP-012, PCP-015) [dev]

| Step | Action | Expected |
|------|--------|----------|
| 1 | Power on | Serial prints `HAT: SPK2` (NOT "ENV III" / "none") |
| 2 | Listen at boot | A short rising 3-note **boot chime** plays through the amp (MEL_BOOT) |
| 3 | Compare loudness | Audio is clearly louder/cleaner than the built-in buzzer |

Fail signals: `HAT: ENV III` (wrong HAT_SELECT or amp not detected as expected),
crash/reboot loop (likely the `M5.Speaker.config()` field names -- see Risks),
silence (routing or wiring).

---

## T1 -- UI sound cues (PCP-013 / SPK-5) [dev]

| Step | Action | Expected |
|------|--------|----------|
| 1 | Press **BtnA** (front) | screen cycles + distinct **click** (MEL_CLICK, 1500 Hz) |
| 2 | Single-click **BtnB** (side) | character changes + **char-switch** tone (1700 Hz) |
| 3 | Double-click **BtnB** | "mute" appears; an **unmute is NOT played** (muting is silent) |
| 4 | Double-click **BtnB** again | "mute" clears + short confirm tone (MEL_MUTE_OFF, 1800 Hz) |

Each cue should be short and crisp, not a long beep.

---

## T2 -- Mute gate (all audio) [dev]

| Step | Action | Expected |
|------|--------|----------|
| 1 | Double-click BtnB to mute | top bar shows "mute" |
| 2 | Press BtnA / BtnB-single | NO sound (cues suppressed) |
| 3 | (with agent) trigger panic | NO siren while muted |
| 4 | Unmute | confirm tone plays; subsequent cues audible again |

Verifies `g_mute` gates `playMelody`, `playVoice`, and all tick* engines.

---

## T3 -- Screen-wake cue (PCP-013) [dev]

| Step | Action | Expected |
|------|--------|----------|
| 1 | Leave idle >30 s until screen is off | backlight off |
| 2 | Shake the stick firmly ~1 s | screen wakes + **wake cue** (MEL_WAKE, 2-note) |
| 3 | While screen already on, shake | screen stays on, **no** wake cue (only fires on off->on) |

---

## T4 -- Panic siren escalation + instant revert (PCP-013 / SPK-3) [pc]

Drive CPU with `python3 tools/cpu_stress.py --percent 95`.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Start stress, pet enters PANIC | **Tier 1**: slow single chirp, repeating (~once/0.6 s) |
| 2 | Keep stress ~10 s | **Tier 2**: faster two-tone alarm |
| 3 | Keep stress ~30 s total | **Tier 3**: near-continuous rising/falling sweep |
| 4 | **Stop** stress (Ctrl+C) | siren **stops within ~1 loop** (instant), pet returns to normal |
| 5 | During the whole siren | **animation keeps moving** -- the pet is NOT frozen (non-blocking proof) |

Fail signals: animation freezes during siren (blocking bug), siren keeps playing
after panic clears (revert bug), tiers don't escalate.

---

## T5 -- Mood ambient loops (PCP-014 / SPK-2) [pc]

Ambient is quiet and only plays in calm moods, with no panic/alert active.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Idle/low CPU (HAPPY) | faint **purr/hum** loop under everything |
| 2 | `cpu_stress.py --percent 60` (BUSY) | quiet **blip-blip** rhythm |
| 3 | Let CPU drop to near 0 (SLEEP) | slow **breathing** ambient |
| 4 | Change mood (e.g. spike then drop) | ambient **fades** out/in across the change -- no click/pop |
| 5 | Trigger panic | ambient yields; **siren takes over** (no overlap) |
| 6 | Mute | ambient silent |

Note: ambient is subtle by design (low channel volume). Listen close, or raise
`AMB_VOL` in `pet_config.h` temporarily to make it obvious.

---

## T6 -- Event voice notifications (PCP-015) [pc + dev]

Synth placeholders for now (real WAV is future work).

| Event | How to trigger | Expected clip |
|-------|----------------|---------------|
| Boot | power on (T0) | MEL_BOOT rising 3-note |
| Overheating | force temp >=75 (agent/synthetic) so mood enters M_HOT | MEL_OVERHEAT descending |
| Low power | host battery <20% & unplugged (or synthetic temp/batt) so mood enters M_LOWPWR | MEL_LOWVOICE low descending |
| Back online | connect agent, let it drop (close agent) then reconnect | MEL_ONLINE rising 4-note on reconnect |

Each fires once on the **transition** into that state, not repeatedly.

---

## T7 -- Heartbeat (PCP-016 / SPK-7) [pc]

Off by default. To test, set `g_heartbeatOn = true` in `pet_state.h` and reflash.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Idle (low CPU) | soft low "thump" at a **slow** rate (~once/1.2 s) |
| 2 | `cpu_stress.py --percent 90` | thump rate **speeds up** (toward ~once/0.3 s) |
| 3 | Mute | heartbeat silent |

---

## T8 -- Reactive chirps (PCP-016 / SPK-8) [pc]

Chirps fire on a **sharp** metric change (>=20 delta in CPU/RAM/GPU, or weighted temp),
rate-limited to one per ~1.5 s.

| Step | Action | Expected |
|------|--------|----------|
| 1 | `cpu_stress.py --ramp` (sudden steps) | a short **chirp** on each big jump; pitch **up** on a rise, **down** on a drop |
| 2 | Steady load (no big changes) | **no** chirps (only sharp changes trigger) |
| 3 | Rapid repeated spikes | chirps are **rate-limited** -- not a constant stream |

---

## T9 -- Regression: ENV III still works [dev]

Swap back to confirm the dual-HAT logic didn't break ENV.

| Step | Action | Expected |
|------|--------|----------|
| 1 | Set `HAT_SELECT HAT_ENV` (or -1), fit ENV III, reflash | Serial: `HAT: ENV III` |
| 2 | Cycle BtnA to ENV screen | live temp/humidity/pressure |
| 3 | Audio cues | play on the **built-in speaker** (no amp) |

---

## Known risks to watch

1. **`M5.Speaker.config()` field names** (`pin_data_out` / `pin_bck` / `pin_ws`)
   vary by M5Unified version. If T0 crashes or is silent, this is the first
   suspect -- check the installed `speaker_config_t`.
2. **Channel API** (`tone(freq, dur, AMB_CH)`, `setChannelVolume`) for ambient
   (T5) also varies by version -- if ambient is silent but cues work, suspect this.
3. **Ambient vs cue balance** is untuned -- expect to adjust `AMB_VOL`.
4. All of Phase 3 is **unverified in code** (no compiler/amp was available when
   written) -- treat first run as a bring-up, not a regression check.

## Result log (fill in)

| Test | Pass/Fail | Notes |
|------|-----------|-------|
| T0 boot+chime | | |
| T1 UI cues | | |
| T2 mute | | |
| T3 wake cue | | |
| T4 siren | | |
| T5 ambient | | |
| T6 voice | | |
| T7 heartbeat | | |
| T8 chirps | | |
| T9 ENV regress | | |
