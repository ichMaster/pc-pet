# Phase 3 -- Execution Report

**Date:** 2026-05-30
**Branch:** v0-dev-hat
**Label:** p3::phase:3
**Executed by:** Claude Code

## Summary

| Status | Count |
|--------|-------|
| Completed | 5 |
| Failed | 0 |
| Skipped | 0 |
| Remaining | 0 |

All 5 issues (#16-#20) implemented and closed. **Unverified:** no Arduino
toolchain or SPK2 amplifier in this environment -- every issue is committed by
inspection (brace-balanced, symbols wired) and flagged for on-device validation.
No version bump pending an actual compile + amp test (see Caveats).

## Issues

| # | PCP ID | Title | Commit | Files |
|---|--------|-------|--------|-------|
| 16 | PCP-012 | SPK2 routing + HAT mode select | 7fe507c | pet_types.h, pc_tamagotchi.ino |
| 17 | PCP-013 | UI cues + non-blocking panic siren | 91dd1a8 | pc_tamagotchi.ino, pet_helpers.ino |
| 18 | PCP-014 | Mood ambient loops | b3a4c0e | pc_tamagotchi.ino, pet_helpers.ino |
| 19 | PCP-015 | WAV/PCM notifications (framework) | 5f0d9c2 | pc_tamagotchi.ino, pet_helpers.ino |
| 20 | PCP-016 | Heartbeat + reactive chirps | 7c3e1f0 | pc_tamagotchi.ino, pet_helpers.ino |

## What was built

- **PCP-012:** `HatMode` + compile-time `HAT_SELECT` (MAX98357 has no I2C
  address). SPK2 routes `M5.Speaker` to the amp's I2S pins (G25/G26/G0) before
  `begin()` and skips the ENV probe; otherwise ENV III auto-probes as in Phase 2.
  Default `HAT_SELECT=-1` keeps current behavior.
- **PCP-013:** `tickSiren()` -- non-blocking, `millis()`-driven looped siren per
  tier (chirp -> two-tone -> continuous sweep), instant stop on revert/mute.
  Replaces the one-shot panic melodies. Plus a `MEL_WAKE` shake-wake cue. This
  is the non-blocking audio scheduler the later issues reuse.
- **PCP-014:** `tickAmbient()` -- quiet per-mood ambient on a dedicated channel
  (`AMB_CH`) with a faded channel volume so it sits under UI cues; yields to the
  siren; SPK2-only.
- **PCP-015:** `playVoice()` + four wired events (boot / overheat / low-power /
  back-online). Clips are **synthesized placeholders**; the documented drop-in
  swaps in real WAV/PCM via `M5.Speaker.playRaw` from SPIFFS/LittleFS/PROGMEM.
- **PCP-016:** `tickHeartbeat()` (CPU-paced soft thump, off by default) +
  `tickChirp()` (sharp-metric-change chirp, pitch by size/direction, cooldown).

## Caveats (must verify on-device)

1. **No compile here.** All firmware is inspection-only. A history of
   silently-broken batched edits in this repo means a real Arduino compile is
   strongly recommended before trusting this build.
2. **M5Unified Speaker API.** The I2S `config()` field names (PCP-012), the
   `tone(..., channel)` arg and `setChannelVolume()` (PCP-014) vary by library
   version -- verify against the installed M5Unified and fix if needed.
3. **PCP-015 assets.** Only synthesized jingles exist; real recorded clips +
   flash storage choice are an on-device follow-up.
4. **Audio balance.** Ambient volume vs. cues/siren, siren timbre, chirp
   thresholds -- all need tuning by ear on the amp.
5. **Heartbeat toggle** is a runtime flag (`g_heartbeatOn`); no physical button
   is wired to it (avoided an untestable BtnA long-press conflict).

## Next steps

1. Fit the SPK2 HAT, set `HAT_SELECT = HAT_SPK2`, compile in the Arduino IDE,
   fix any M5Unified API mismatches.
2. Confirm routing (existing beeps via amp), then siren, ambient, voice events,
   heartbeat/chirps by ear.
3. Add real WAV assets for PCP-015.
4. Then bump version (Phase 3 -> v0.3.0) and tag.
