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
