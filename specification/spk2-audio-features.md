# Specification — SPK2 Audio Features

Status: proposal (not yet implemented). No firmware changes are described here as
final; this document defines *what* the SPK2 HAT could add and *how the features
behave*, for later implementation under the ROADMAP.

## Hardware

- HAT: M5Stack Hat SPK2 — MAX98357 I2S Class-D amplifier + 1 W speaker.
  Supports 16-bit PCM, 32-192 kHz. Plays tones and arbitrary PCM/WAV, not just beeps.
- Pins on the StickC Plus2 HAT connector: DATA (DOUT) = G25, BCLK = G26, LRC (WS) = G0.

### Hard constraint — mutually exclusive with ENV III
The StickC Plus2 has a single HAT port, and SPK2 (I2S on G0/G25/G26) overlaps the
ENV III HAT (I2C on G0/G26). Only one HAT can be fitted at a time, and their pins
collide. Therefore:
- A build session runs **either** ENV III (feature #7, environment sensing) **or**
  SPK2 (these audio features) — never both.
- Firmware should detect which HAT is present at boot (presence check on the I2C
  address for ENV vs. I2S init for SPK2) and enable the matching feature set.

## Design principle — SPK2 is just another output

The existing sound system (button beeps, mood alerts, low-battery alert, and the
planned 8a melody signatures) does not change in logic. SPK2 only changes *where*
the audio is rendered: the same calls route to the I2S amplifier instead of the
built-in speaker. Every feature below is additive and reuses that one output path.

---

## Features

### SPK-1. Audio output routing (foundation)
- **Goal:** route the device's audio to the SPK2 amplifier when the HAT is present.
- **Behavior:** on boot, if SPK2 is detected, audio output targets the I2S pins
  (G25/G26/G0); otherwise it falls back to the built-in speaker. All existing
  tones and the 8a melodies play unchanged, just louder and clearer.
- **Dependencies:** none. Enables every other SPK feature.
- **Effort:** low.

### SPK-2. Mood ambient loops ("the pet has a voice")
- **Goal:** give each mood a continuous, low-volume ambient sound so the creature
  feels alive, not just animated.
- **Behavior:**
  - HAPPY: soft purr / gentle hum.
  - SLEEP: slow breathing / light snore, synced to the Zzz animation.
  - BUSY: a quiet working "blip-blip" rhythm.
  - STUFFED: occasional content sigh.
  - LOWPWR: faint, slowing whimper as battery drops.
  Loops are quiet by default, fade in/out on mood change, and respect mute.
- **Dependencies:** SPK-1; mood state (exists). Pairs with 8a.
- **Effort:** medium (looping + smooth transitions without clicks).
- **Open questions:** synthesized tones vs. short looped WAV samples; default
  ambient volume relative to alerts.

### SPK-3. Panic siren with tier escalation
- **Goal:** make a sustained PC panic genuinely attention-grabbing, scaling with
  the #2 panic tiers.
- **Behavior:**
  - PANIC-1: short repeating chirp.
  - PANIC-2: a two-tone alarm, faster cadence.
  - PANIC-3: a full rising/falling siren that does not stop until panic clears.
  Instant silence the moment metrics drop out of panic (matches the #2 instant
  revert). Always respects mute.
- **Dependencies:** SPK-1; #2 panic tiers; 8a melody helper.
- **Effort:** medium.
- **Open questions:** whether PANIC-3 should also flash the screen in sync.

### SPK-4. Sampled voice / WAV notifications
- **Goal:** play short recorded clips for key events (only practical with a real
  amplifier).
- **Behavior:** store small WAV/PCM assets in flash (SPIFFS/LittleFS) and play on
  events: boot chime, "overheating", "low power", "back online" when the BLE link
  recovers. Clips are short (well under a second to a few seconds) to fit flash.
- **Dependencies:** SPK-1; a small asset bundle in flash.
- **Effort:** medium (asset prep + flash layout).
- **Open questions:** asset format/size budget; which events get a voice vs. a tone.

### SPK-5. UI sound feedback
- **Goal:** tactile-feeling audio cues for on-device interaction.
- **Behavior:** distinct short sounds for screen switch (BtnA), character change
  (BtnB single), mute toggle (BtnB double — a confirming sound even as it mutes
  the rest), and screen wake. Subtle, never fatiguing.
- **Dependencies:** SPK-1; existing button handling.
- **Effort:** low.

### SPK-6. Agent-event sounds
- **Goal:** sound cues for events coming from the host agent, as later features land.
- **Behavior:**
  - Disk I/O (#6): an optional faint "seek" tick on heavy read/write bursts.
  - Reverse channel (5a): a click/confirm when a remote command (lock, play-pause,
    volume, run-script) is issued from the stick.
  - Kill (5b): a deliberate, distinct confirmation tone — clearly different from
    the safe-command click, to match its irreversible nature.
- **Dependencies:** SPK-1; the respective features (#6, 5a, 5b).
- **Effort:** low (incremental, per feature).

### SPK-7. Ambient clock / heartbeat
- **Goal:** an optional life-sign and timekeeping touch.
- **Behavior:** a very soft "heartbeat" tick whose rate follows CPU load (calm when
  idle, quicker when busy), and/or an optional hourly chime. Off by default.
- **Dependencies:** SPK-1; 1.1 one-second tick (for timing).
- **Effort:** low-medium.
- **Open questions:** tie tick rate to CPU, to mood, or keep it constant.

### SPK-8. Reactive chirps ("commentary")
- **Goal:** a playful R2-D2-style reaction when metrics change sharply.
- **Behavior:** on a fast spike or drop (CPU, RAM, GPU, temperature), emit a short
  generated chirp whose pitch/length maps to the size and direction of the change.
  Rate-limited so it stays charming, not noisy. Respects mute.
- **Dependencies:** SPK-1; metric history (CPU history exists; extend as needed).
- **Effort:** medium.
- **Open questions:** per-metric chirp signatures; cooldown window.

### SPK-9. Volume policy
- **Goal:** sensible loudness without a settings menu.
- **Behavior:** baseline volume for ambient/UI; alerts (SPK-3, low-battery) step
  up automatically; a quiet-hours window can cap volume. Mute (BtnB double-click)
  overrides everything except, optionally, a critical PANIC-3 / low-power warning
  (configurable).
- **Dependencies:** SPK-1; 8b config channel (to set levels from the agent).
- **Effort:** low-medium.
- **Open questions:** does mute fully silence critical alerts, or only attenuate them.

---

## Relationship to existing ROADMAP

- **8a (buzzer patterns):** SPK features are the richer output for the same melody
  system. 8a defines the signatures; SPK-1 routes them to the amplifier; SPK-2/3
  extend them into loops and sirens.
- **#2 (panic tiers):** SPK-3 is the audio half of the escalation.
- **#6 / 5a / 5b:** SPK-6 adds their event sounds as those features land.
- **8b (config channel):** SPK-9 reads volume/quiet-hours from the agent config.
- **#7 (ENV III):** mutually exclusive hardware — document the either/or clearly.

## Suggested phasing

1. SPK-1 (routing + HAT detection) — unlocks everything.
2. SPK-5 (UI feedback) and SPK-3 (panic siren) — fast, high-impact for demos.
3. SPK-2 (mood ambient loops) — the "alive" factor.
4. SPK-4 (WAV notifications) — once a flash asset budget is set.
5. SPK-6 / SPK-7 / SPK-8 / SPK-9 — incremental polish alongside other features.

## Open questions (summary)

1. Synthesized tones vs. WAV samples for ambient/voice (SPK-2, SPK-4).
2. Flash budget and asset format for samples (SPK-4).
3. Whether mute silences critical alerts entirely (SPK-9).
4. HAT auto-detection method to switch cleanly between ENV III and SPK2 modes.
