// pet_helpers.ino -- logic + small drawing helpers for PC-Pet.
//
// Additional Arduino "tab": the IDE concatenates it onto the main sketch, so
// the globals/types declared in pc_tamagotchi.ino and pet_types.h are already
// visible here. Do not add includes or re-declare the globals.

void playMelody(const MelNote* mel) {
  if (g_mute || !mel) return;
  for (int i = 0; mel[i].freq != 0; i++) {
    M5.Speaker.tone(mel[i].freq, mel[i].durMs);
    delay(mel[i].durMs + mel[i].pauseMs);
  }
}

// Non-blocking panic siren (PCP-013). Call every loop with the current panic
// tier (0 = not panicking). It advances one looped note per call based on
// millis(), so the animation never freezes. Instant silence when tier drops to
// 0 (matches the #2 instant-revert rule) or when muted.
void tickSiren(int tier) {
  if (tier < 1 || g_mute) {
    if (g_sirenTier != 0) { M5.Speaker.stop(); g_sirenTier = 0; g_sirenIdx = 0; }
    return;
  }
  if (tier != g_sirenTier) {      // tier changed -> restart pattern immediately
    g_sirenTier = tier;
    g_sirenIdx  = 0;
    g_sirenNext = 0;
  }
  if (millis() < g_sirenNext) return;
  const MelNote* pat = (tier >= 3) ? SIREN_T3 : (tier == 2) ? SIREN_T2 : SIREN_T1;
  if (pat[g_sirenIdx].freq == 0) g_sirenIdx = 0;   // loop back to the start
  const MelNote& n = pat[g_sirenIdx];
  M5.Speaker.tone(n.freq, n.durMs);
  g_sirenNext = millis() + n.durMs + n.pauseMs;
  g_sirenIdx++;
}

// Event voice notification (PCP-015). Amp-only (SPK2). Plays a short clip for
// boot / overheat / low-power / back-online events.
// DROP-IN for real audio: replace the playMelody() call with
//   M5.Speaker.playRaw(pcm, len, sampleRate, false);   // from SPIFFS/PROGMEM
// The synthesized jingle is a placeholder until recorded assets exist.
void playVoice(const MelNote* mel) {
  if (g_hat != HAT_SPK2) return;
  playMelody(mel);
}

// Soft heartbeat whose rate follows CPU load (PCP-016). Off by default
// (g_heartbeatOn); SPK2 only; non-blocking (one async tone per beat).
void tickHeartbeat(int cpu) {
  if (!g_heartbeatOn || g_hat != HAT_SPK2 || g_mute) return;
  uint32_t interval = 1200 - (uint32_t)(constrain(cpu, 0, 100) * 9);  // 1200->300ms
  if (interval < 300) interval = 300;
  if (millis() - g_lastBeat >= interval) {
    g_lastBeat = millis();
    M5.Speaker.tone(110, 35);   // soft low "thump"
  }
}

// Reactive chirp on a sharp metric change (PCP-016). Pitch maps to size +
// direction; rate-limited by CHIRP_COOLDOWN. SPK2 only; non-blocking.
void tickChirp(int cpu, int ram, int gpu, int temp) {
  if (g_hat != HAT_SPK2 || g_mute) return;
  if (millis() - g_lastChirp >= CHIRP_COOLDOWN) {
    int dCpu  = cpu - g_pchCpu;
    int dRam  = ram - g_pchRam;
    int dGpu  = (gpu  < 0 ? 0 : gpu)  - (g_pchGpu  < 0 ? 0 : g_pchGpu);
    int dTemp = (temp < 0 ? 0 : temp) - (g_pchTemp < 0 ? 0 : g_pchTemp);
    int best = dCpu;                                  // largest-magnitude change
    if (abs(dRam) > abs(best))      best = dRam;
    if (abs(dGpu) > abs(best))      best = dGpu;
    if (abs(dTemp) * 3 > abs(best)) best = dTemp * 3; // temp weighted (small range)
    if (abs(best) >= 20) {
      int freq = constrain(1500 + best * 12, 400, 3500);  // up=higher, down=lower
      M5.Speaker.tone(freq, 60);
      g_lastChirp = millis();
    }
  }
  g_pchCpu = cpu; g_pchRam = ram; g_pchGpu = gpu; g_pchTemp = temp;
}

// Non-blocking mood ambient loop (PCP-014). Quiet per-mood "voice" on a
// dedicated channel (AMB_CH) so it sits under the UI cues; fades in/out on
// mood change; off when not SPK2, muted, or panicking (the siren owns audio).
// NOTE: channel/volume API names (tone channel arg, setChannelVolume) vary by
// M5Unified version -- verify on-device.
void tickAmbient(Mood mood, int tier) {
  const MelNote* pat = nullptr;
  switch (mood) {
    case M_HAPPY:   pat = AMB_HAPPY;   break;
    case M_SLEEP:   pat = AMB_SLEEP;   break;
    case M_BUSY:    pat = AMB_BUSY;    break;
    case M_STUFFED: pat = AMB_STUFFED; break;
    case M_LOWPWR:  pat = AMB_LOWPWR;  break;
    default:        pat = nullptr;     break;   // HOT / PANIC: no ambient
  }
  bool active = (g_hat == HAT_SPK2) && !g_mute && tier == 0 && pat != nullptr;

  if (!active) {                          // fade out, then go silent
    if (g_ambVol > 0) {
      g_ambVol = (g_ambVol > 4) ? g_ambVol - 4 : 0;
      M5.Speaker.setChannelVolume(AMB_CH, g_ambVol);
      if (g_ambVol == 0) g_ambMood = -1;
    }
    return;
  }

  if ((int)mood != g_ambMood) {           // mood changed -> restart pattern
    g_ambMood = mood;
    g_ambIdx  = 0;
    g_ambNext = 0;
  }
  if (g_ambVol < AMB_VOL) g_ambVol += 2;  // fade in
  M5.Speaker.setChannelVolume(AMB_CH, g_ambVol);

  if (millis() < g_ambNext) return;
  if (pat[g_ambIdx].freq == 0) g_ambIdx = 0;   // loop
  const MelNote& n = pat[g_ambIdx];
  M5.Speaker.tone(n.freq, n.durMs, AMB_CH);
  g_ambNext = millis() + n.durMs + n.pauseMs;
  g_ambIdx++;
}

uint16_t lerpColor(uint16_t a, uint16_t b, float t) {
  // simple 565 blend
  int ar = (a >> 11) & 0x1F, ag = (a >> 5) & 0x3F, ab = a & 0x1F;
  int br = (b >> 11) & 0x1F, bg = (b >> 5) & 0x3F, bb = b & 0x1F;
  int r = ar + (br - ar) * t;
  int g = ag + (bg - ag) * t;
  int bl = ab + (bb - ab) * t;
  return (r << 11) | (g << 5) | bl;
}

Mood currentMood(int cpu, int ram, int temp, int gpu, int batt, int charging) {
  if (temp >= 75)                            return M_HOT;
  if (cpu >= 85 || ram >= 92 || gpu >= 95)   return M_PANIC;
  if (batt >= 0 && batt < 20 && !charging)   return M_LOWPWR;
  if (ram >= 85)                             return M_STUFFED;
  if (cpu >= 50 || gpu >= 60)                return M_BUSY;
  if (cpu < 15 && gpu < 15)                  return M_SLEEP;
  return M_HAPPY;
}

uint16_t bodyColor(Mood m) {
  switch (m) {
    case M_SLEEP:   return canvas.color565(120, 150, 230);
    case M_HAPPY:   return canvas.color565(120, 215, 140);
    case M_BUSY:    return canvas.color565(245, 190,  70);
    case M_STUFFED: return canvas.color565(200, 160, 120);
    case M_HOT:     return canvas.color565(255, 130,  70);
    case M_PANIC:   return canvas.color565(245,  90,  80);
    case M_LOWPWR:  return canvas.color565(150, 140, 160);   // muted, drained
  }
  return TFT_WHITE;
}

// ---- panic tiers (PCP-003) ----
const uint32_t PANIC_T1 = 10;
const uint32_t PANIC_T2 = 30;

int panicTier(uint32_t sec) {
  if (sec >= PANIC_T2) return 3;
  if (sec >= PANIC_T1) return 2;
  return 1;
}

// Pressure trend (PCP-007): +1 rising / -1 falling / 0 steady, over the logged
// window. Returns 0 until enough samples exist (no false trend).
int pressTrend() {
  if (g_pressHistN < 3) return 0;
  int oldest = (g_pressHistN < PRESS_HIST)
                 ? 0
                 : g_pressHistPos;                 // ring start when full
  int newest = (g_pressHistPos - 1 + PRESS_HIST) % PRESS_HIST;
  float delta = g_pressHist[newest] - g_pressHist[oldest];
  if (delta > 0.5f)  return 1;
  if (delta < -0.5f) return -1;
  return 0;
}

// ENV mood modifier (PCP-008). Thresholds hardcoded for now (8b may expose them).
const float ENV_STUFFY_TEMP = 27.0f;   // room temp C
const float ENV_STUFFY_HUM  = 60.0f;   // humidity %


// STUFFY when the room is hot AND humid; WEATHER when pressure is falling
// sharply. Only meaningful when the HAT is present.
EnvMod envModifier() {
  if (!g_envPresent) return ENV_NONE;
  if (g_envTemp >= ENV_STUFFY_TEMP && g_envHum >= ENV_STUFFY_HUM) return ENV_STUFFY;
  if (pressTrend() < 0) return ENV_WEATHER;
  return ENV_NONE;
}

// draw a small horizontal bar
void drawBar(int x, int y, int w, int h, int pct, uint16_t col) {
  canvas.drawRoundRect(x, y, w, h, 2, canvas.color565(70, 70, 80));
  int fill = (w - 2) * constrain(pct, 0, 100) / 100;
  canvas.fillRoundRect(x + 1, y + 1, fill, h - 2, 2, col);
}
