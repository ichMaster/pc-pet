// pet_mood.ino -- mood, character names, and environment logic for PC-Pet.
//
// Arduino tab. Pure decision logic: metrics + sensor state -> Mood, panic tier,
// body colour, pressure trend, ENV modifier. Thresholds in pet_config.h; state
// in pet_state.h.

// Mood word shown under the pet; panic tier (1-3) selects the panic variant.
const char* moodWord(Mood m, int tier = 1) {
  switch (m) {
    case M_SLEEP:   return "zzz...";
    case M_HAPPY:   return "chillin";
    case M_BUSY:    return "workin'";
    case M_STUFFED: return "so full";
    case M_HOT:     return "too hot!";
    case M_PANIC:
      if (tier >= 3) return "CRITICAL";
      if (tier >= 2) return "PANIC!!!";
      return "PANIC!!";
    case M_LOWPWR:  return "low pwr";
  }
  return "";
}

// Character name for the top bar (cycled with BtnB single click).
const char* charName(int c) {
  switch (c) {
    case 1: return "Cat";
    case 2: return "Robo";
    case 3: return "Ghost";
    case 4: return "Bunny";
  }
  return "Blobby";
}

// Mood from the latest metrics. First match wins (priority top to bottom).
Mood currentMood(int cpu, int ram, int temp, int gpu, int batt, int charging) {
  if (temp >= 75)                            return M_HOT;
  if (cpu >= 85 || ram >= 92 || gpu >= 95)   return M_PANIC;
  if (batt >= 0 && batt < 20 && !charging)   return M_LOWPWR;
  if (ram >= 85)                             return M_STUFFED;
  if (cpu >= 50 || gpu >= 60)                return M_BUSY;
  if (cpu < 15 && gpu < 15)                  return M_SLEEP;
  return M_HAPPY;
}

// Body colour per mood (RGB565 via the canvas helper).
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

// Panic tier from continuous-panic seconds (PCP-003).
int panicTier(uint32_t sec) {
  if (sec >= PANIC_T2) return 3;
  if (sec >= PANIC_T1) return 2;
  return 1;
}

// Pressure trend (PCP-007): +1 rising / -1 falling / 0 steady over the logged
// window. Returns 0 until enough samples exist (no false trend).
int pressTrend() {
  if (g_pressHistN < 3) return 0;
  int oldest = (g_pressHistN < PRESS_HIST) ? 0 : g_pressHistPos;
  int newest = (g_pressHistPos - 1 + PRESS_HIST) % PRESS_HIST;
  float delta = g_pressHist[newest] - g_pressHist[oldest];
  if (delta > 0.5f)  return 1;
  if (delta < -0.5f) return -1;
  return 0;
}

// ENV mood modifier (PCP-008): STUFFY when hot+humid, WEATHER when pressure is
// falling sharply. Only meaningful when the HAT is present.
EnvMod envModifier() {
  if (!g_envPresent) return ENV_NONE;
  if (g_envTemp >= ENV_STUFFY_TEMP && g_envHum >= ENV_STUFFY_HUM) return ENV_STUFFY;
  if (pressTrend() < 0) return ENV_WEATHER;
  return ENV_NONE;
}
