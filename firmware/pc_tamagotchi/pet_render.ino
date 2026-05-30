// pet_render.ino -- character art, the pet renderer, and all view screens.
//
// Additional Arduino "tab": concatenated onto the main sketch, so globals/types
// from pc_tamagotchi.ino and pet_types.h are visible here. Do not add includes.

// ----------- character silhouettes ---------------------------
// Draws the body + character-specific features. Eyes/mouth/effects
// are drawn afterwards by drawPet and are shared across characters.
void drawCharBody(int cx, int cy, int rx, int ry, uint16_t col, int charId) {
  uint16_t pink = lerpColor(col, canvas.color565(255, 150, 170), 0.5f);
  switch (charId) {
    case 1:  // Cat
      canvas.fillTriangle(cx - rx + 4, cy - ry + 4, cx - rx + 24, cy - ry,
                          cx - rx + 6, cy - ry - 18, col);
      canvas.fillTriangle(cx + rx - 4, cy - ry + 4, cx + rx - 24, cy - ry,
                          cx + rx - 6, cy - ry - 18, col);
      canvas.fillEllipse(cx, cy, rx, ry, col);
      canvas.fillTriangle(cx - rx + 9, cy - ry - 1, cx - rx + 18, cy - ry - 2,
                          cx - rx + 9, cy - ry - 11, pink);
      canvas.fillTriangle(cx + rx - 9, cy - ry - 1, cx + rx - 18, cy - ry - 2,
                          cx + rx - 9, cy - ry - 11, pink);
      // whiskers
      canvas.drawFastHLine(cx - rx - 6, cy + 6, 10, lerpColor(col, TFT_WHITE, 0.5f));
      canvas.drawFastHLine(cx + rx - 4, cy + 6, 10, lerpColor(col, TFT_WHITE, 0.5f));
      break;
    case 2:  // Robo
      canvas.drawFastVLine(cx, cy - ry - 12, 12, canvas.color565(180, 184, 196));
      canvas.fillCircle(cx, cy - ry - 14, 3, canvas.color565(255, 90, 80));
      canvas.fillRoundRect(cx - rx, cy - ry, rx * 2, ry * 2, 7, col);
      canvas.drawFastHLine(cx - rx + 5, cy + ry - 9, rx * 2 - 10,
                           lerpColor(col, TFT_BLACK, 0.25f));
      canvas.fillCircle(cx - rx + 6, cy - ry + 6, 2, lerpColor(col, TFT_BLACK, 0.30f));
      canvas.fillCircle(cx + rx - 6, cy - ry + 6, 2, lerpColor(col, TFT_BLACK, 0.30f));
      break;
    case 3: {  // Ghost
      canvas.fillEllipse(cx, cy, rx, ry, col);
      canvas.fillRect(cx - rx, cy, rx * 2, ry, col);
      int bw = (rx * 2) / 4;
      for (int i = 0; i < 4; i++)
        canvas.fillCircle(cx - rx + bw / 2 + i * bw, cy + ry, bw / 2, col);
      break;
    }
    case 4:  // Bunny
      canvas.fillRoundRect(cx - 14, cy - ry - 26, 9, 30, 4, col);
      canvas.fillRoundRect(cx + 5,  cy - ry - 26, 9, 30, 4, col);
      canvas.fillRoundRect(cx - 12, cy - ry - 22, 5, 22, 2, pink);
      canvas.fillRoundRect(cx + 7,  cy - ry - 22, 5, 22, 2, pink);
      canvas.fillEllipse(cx, cy, rx, ry, col);
      break;
    default:  // Blobby
      canvas.fillEllipse(cx, cy, rx, ry, col);
      canvas.fillEllipse(cx - rx / 3, cy - ry / 3, rx / 5, ry / 6,
                         lerpColor(col, TFT_WHITE, 0.4f));
      break;
  }
}

// ----------- the creature ------------------------------------
void drawPet(int cx, int cy, Mood mood, uint32_t frame) {
  float t = frame * 0.18f;
  int tier = (mood == M_PANIC) ? panicTier(g_panicSec) : 0;
  uint16_t col = bodyColor(mood);
  if (tier == 2) col = lerpColor(col, canvas.color565(180, 50, 40), 0.5f);
  if (tier == 3) col = lerpColor(col, canvas.color565(140, 120, 130), 0.7f);

  // motion per mood
  float breathe = 1.0f + 0.05f * sinf(t);      // idle breathing
  int   jitter  = 0;                            // panic shake
  int   bounceY = 0;
  if (mood == M_PANIC) jitter = (frame % 2) ? (tier >= 2 ? 4 : 2) : -(tier >= 2 ? 4 : 2);
  if (mood == M_BUSY)  bounceY = (int)(3 * fabsf(sinf(t * 1.6f)));
  if (mood == M_HAPPY) bounceY = (int)(2 * sinf(t));

  cx += jitter;
  cy -= bounceY;

  int rx = (int)(34 * breathe);
  int ry = (int)(30 * breathe);

  // soft shadow
  canvas.fillEllipse(cx, cy + ry + 8, rx - 4, 5, canvas.color565(30, 32, 40));

  // little legs while working
  if (mood == M_BUSY) {
    int step = (frame % 12 < 6) ? 6 : -6;
    canvas.fillRoundRect(cx - 16 + step, cy + ry - 4, 8, 14, 3, col);
    canvas.fillRoundRect(cx + 8 - step,  cy + ry - 4, 8, 14, 3, col);
  }

  // body (character-specific silhouette + features)
  drawCharBody(cx, cy, rx, ry, col, g_char);

  // eyes
  bool blink = (frame % 90) < 6 && (mood == M_HAPPY || mood == M_BUSY);
  int  ex = 13, ey = -6, er = 7;
  if (mood == M_SLEEP) {
    // closed, curved eyes
    canvas.drawArc(cx - ex, cy + ey, er, er - 2, 200, 340, TFT_BLACK);
    canvas.drawArc(cx + ex, cy + ey, er, er - 2, 200, 340, TFT_BLACK);
  } else if (blink) {
    canvas.drawFastHLine(cx - ex - er + 1, cy + ey, er * 2 - 2, TFT_BLACK);
    canvas.drawFastHLine(cx + ex - er + 1, cy + ey, er * 2 - 2, TFT_BLACK);
  } else if (mood == M_LOWPWR) {
    // tired, half-closed eyes
    canvas.fillCircle(cx - ex, cy + ey + 1, er - 1, TFT_WHITE);
    canvas.fillCircle(cx + ex, cy + ey + 1, er - 1, TFT_WHITE);
    canvas.fillCircle(cx - ex, cy + ey + 3, 2, TFT_BLACK);
    canvas.fillCircle(cx + ex, cy + ey + 3, 2, TFT_BLACK);
    canvas.fillRect(cx - ex - er, cy + ey - er, er * 2, er, col);   // heavy lids
    canvas.fillRect(cx + ex - er, cy + ey - er, er * 2, er, col);
  } else if (tier == 3) {
    canvas.fillCircle(cx - ex, cy + ey + 1, er - 1, TFT_WHITE);
    canvas.fillCircle(cx + ex, cy + ey + 1, er - 1, TFT_WHITE);
    canvas.fillCircle(cx - ex, cy + ey + 3, 2, TFT_BLACK);
    canvas.fillCircle(cx + ex, cy + ey + 3, 2, TFT_BLACK);
    canvas.fillRect(cx - ex - er, cy + ey - er, er * 2, er, col);
    canvas.fillRect(cx + ex - er, cy + ey - er, er * 2, er, col);
  } else {
    int wide = (mood == M_PANIC || mood == M_HOT) ? 2 : 0;
    canvas.fillCircle(cx - ex, cy + ey, er + wide, TFT_WHITE);
    canvas.fillCircle(cx + ex, cy + ey, er + wide, TFT_WHITE);
    int pup = (mood == M_PANIC) ? 2 : 3;
    int look = (mood == M_BUSY) ? ((frame % 24 < 12) ? 2 : -2) : 0;
    canvas.fillCircle(cx - ex + look, cy + ey, pup, TFT_BLACK);
    canvas.fillCircle(cx + ex + look, cy + ey, pup, TFT_BLACK);
  }

  // mouth
  int my = cy + 12;
  switch (mood) {
    case M_HAPPY:
      canvas.drawArc(cx, my - 4, 10, 8, 20, 160, TFT_BLACK); break;
    case M_SLEEP:
      canvas.fillEllipse(cx, my, 4, 3, canvas.color565(60, 60, 90)); break;
    case M_BUSY:
      canvas.fillCircle(cx, my, 4, TFT_BLACK); break;
    case M_STUFFED:
      canvas.drawFastHLine(cx - 8, my, 16, TFT_BLACK); break;
    case M_LOWPWR:
      canvas.drawArc(cx, my + 6, 10, 8, 200, 340, TFT_BLACK); break;   // slight frown
    case M_HOT:
    case M_PANIC:
      if (tier == 3) {
        for (int i = -8; i <= 8; i++)
          canvas.fillCircle(cx + i, my + (int)(3 * sinf(i * 0.6f + t)), 1, TFT_BLACK);
      } else {
        canvas.fillEllipse(cx, my + 2, 7, 9, TFT_BLACK);
        canvas.fillEllipse(cx, my + 4, 4, 4, canvas.color565(200, 60, 60));
      }
      break;
  }

  // sweat drops when hot / panic
  if (mood == M_HOT || mood == M_PANIC) {
    int dy = (frame % 20);
    canvas.fillCircle(cx + rx - 2, cy - 6 + dy, 3, canvas.color565(120, 200, 255));
    canvas.fillCircle(cx - rx + 1, cy - 2 + (dy + 8) % 20, 2,
                      canvas.color565(120, 200, 255));
    if (tier >= 2) {
      canvas.fillCircle(cx + rx - 8, cy - 10 + (dy + 5) % 20, 2, canvas.color565(120, 200, 255));
      canvas.fillCircle(cx - rx + 7, cy - 8  + (dy + 12) % 20, 2, canvas.color565(120, 200, 255));
    }
  }

  // tier 3: pulsing red overlay
  if (tier == 3) {
    float pulse = 0.15f + 0.1f * sinf(t * 0.5f);
    uint16_t overlay = lerpColor(col, canvas.color565(255, 40, 40), pulse);
    canvas.fillEllipse(cx, cy, rx - 4, ry - 4, overlay);
  }

  // Zzz when sleeping
  if (mood == M_SLEEP) {
    canvas.setTextColor(canvas.color565(150, 170, 230));
    canvas.setTextSize(1);
    int zy = cy - ry - 6 - (frame % 18);
    canvas.setTextDatum(middle_center);
    canvas.drawString("z", cx + rx, zy);
    canvas.drawString("Z", cx + rx + 8, zy - 8);
  }

  // stuffed cheeks puff
  if (mood == M_STUFFED) {
    canvas.fillCircle(cx - rx + 2, cy + 4, 6, lerpColor(col, TFT_WHITE, 0.15f));
    canvas.fillCircle(cx + rx - 2, cy + 4, 6, lerpColor(col, TFT_WHITE, 0.15f));
  }
}

// =================  view renderers  ===========================
void renderTopBar(int cpu, int ram, int temp, int net, int procs,
                  const char* top, bool connected) {
  int W = canvas.width();
  // connection dot
  canvas.fillCircle(8, 9, 4, connected ? canvas.color565(90, 220, 120)
                                        : canvas.color565(220, 90, 90));
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(canvas.color565(170, 175, 190));
  canvas.setTextSize(1);
  canvas.drawString(connected ? "BLE" : "...", 16, 9);

  // battery
  int bat = M5.Power.getBatteryLevel();
  canvas.setTextDatum(middle_right);
  char bs[12];
  if (bat >= 0 && bat < 10) {
    snprintf(bs, sizeof(bs), "! %d%%", bat);
    canvas.setTextColor(canvas.color565(245, 90, 80));   // red when low
  } else {
    snprintf(bs, sizeof(bs), "%d%%", bat);
    canvas.setTextColor(canvas.color565(170, 175, 190));
  }
  canvas.drawString(bs, W - 4, 9);
  canvas.setTextDatum(middle_center);
  if (g_mute) {
    canvas.setTextColor(canvas.color565(230, 180, 90));
    canvas.drawString("mute", W / 2, 9);
  } else {
    canvas.setTextColor(canvas.color565(150, 155, 170));
    canvas.drawString(charName(g_char), W / 2, 9);
  }
}

void viewPet(int cpu, int ram, int temp, int net, int procs,
             const char* top, int gpu, int batt, int charging,
             bool connected, uint32_t frame) {
  Mood mood = connected ? currentMood(cpu, ram, temp, gpu, batt, charging) : M_SLEEP;

  // ENV mood modifier (PCP-008): only nudges the display when the PC mood is
  // calm (M_HAPPY). PC alert states (PANIC/HOT/LOWPWR/STUFFED/BUSY) always win.
  // STUFFY shows the stuffed face; WEATHER only shows a small badge.
  EnvMod emod = (mood == M_HAPPY) ? envModifier() : ENV_NONE;
  if (emod == ENV_STUFFY) mood = M_STUFFED;

  // background tint subtly follows mood
  uint16_t bg = lerpColor(canvas.color565(16, 18, 24),
                          bodyColor(mood), connected ? 0.10f : 0.0f);
  canvas.fillScreen(bg);
  renderTopBar(cpu, ram, temp, net, procs, top, connected);

  drawPet(canvas.width() / 2, 96, mood, frame);

  // mood word
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextSize(2);
  int tier = (mood == M_PANIC) ? panicTier(g_panicSec) : 1;
  canvas.drawString(connected ? moodWord(mood, tier) : "waiting", canvas.width() / 2, 150);

  // top process
  canvas.setTextSize(1);
  canvas.setTextColor(canvas.color565(160, 165, 180));
  if (connected) {
    char line[24];
    snprintf(line, sizeof(line), "busiest: %s", top);
    canvas.drawString(line, canvas.width() / 2, 170);
  }

  // mini bars
  int bx = 12, bw = canvas.width() - 24;
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(canvas.color565(150, 155, 170));
  canvas.drawString("CPU", bx, 190);
  drawBar(bx + 28, 186, bw - 28, 8, cpu, canvas.color565(245, 190, 70));
  canvas.drawString("RAM", bx, 206);
  drawBar(bx + 28, 202, bw - 28, 8, ram, canvas.color565(120, 200, 255));
  // footer: temp, GPU, PC battery
  char foot[36], ts[8], gs[8], pb[10];
  if (temp >= 0) snprintf(ts, sizeof(ts), "%dC", temp); else strcpy(ts, "--C");
  if (gpu  >= 0) snprintf(gs, sizeof(gs), "G%d", gpu);  else strcpy(gs, "G--");
  if (batt >= 0) snprintf(pb, sizeof(pb), "%s%d%%", charging ? "+" : "", batt);
  else           strcpy(pb, "B--");
  snprintf(foot, sizeof(foot), "%s  %s  %s", ts, gs, pb);
  canvas.setTextDatum(middle_center);
  canvas.drawString(foot, canvas.width() / 2, 224);

  // ENV modifier badge (PCP-008): subtle, top-left under the bar
  if (emod != ENV_NONE) {
    canvas.setTextDatum(top_left);
    canvas.setTextColor(canvas.color565(200, 180, 120));
    canvas.drawString(emod == ENV_STUFFY ? "stuffy" : "weather", 4, 20);
  }
}

void viewStats(int cpu, int ram, int temp, int net, int procs,
               const char* top, int gpu, int batt, int charging, bool connected,
               int diskR, int diskW) {
  canvas.fillScreen(canvas.color565(16, 18, 24));
  renderTopBar(cpu, ram, temp, net, procs, top, connected);

  canvas.setTextDatum(top_left);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextSize(2);
  canvas.drawString("Stats", 10, 22);

  int y = 46, bx = 10, bw = canvas.width() - 20;
  canvas.setTextSize(1);
  auto row = [&](const char* lbl, int val, const char* unit, uint16_t col) {
    canvas.setTextColor(canvas.color565(170, 175, 190));
    canvas.setTextDatum(top_left);
    canvas.drawString(lbl, bx, y);
    char v[12];
    if (val < 0) snprintf(v, sizeof(v), "--");
    else snprintf(v, sizeof(v), "%d%s", val, unit);
    canvas.setTextDatum(top_right);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(v, bx + bw, y);
    drawBar(bx, y + 12, bw, 7, (val < 0 ? 0 : val), col);
    y += 30;
  };
  row("CPU",  cpu,  "%", canvas.color565(245, 190, 70));
  row("RAM",  ram,  "%", canvas.color565(120, 200, 255));
  row("GPU",  gpu,  "%", canvas.color565(180, 140, 250));
  row("TEMP", temp, "C", canvas.color565(255, 130, 70));

  canvas.setTextDatum(top_left);
  canvas.setTextColor(canvas.color565(170, 175, 190));
  char lb[28];
  if (batt >= 0) snprintf(lb, sizeof(lb), "batt: %d%%%s", batt, charging ? "  (chg)" : "");
  else           snprintf(lb, sizeof(lb), "batt: --");
  canvas.drawString(lb, bx, y);       y += 14;
  char ds[28];
  snprintf(ds, sizeof(ds), "disk: R%d W%d MB/s", diskR, diskW);
  canvas.drawString(ds, bx, y);       y += 14;
  char l1[24], l3[28];
  snprintf(l1, sizeof(l1), "net : %d KB/s   %dp", net, procs);
  canvas.drawString(l1, bx, y);       y += 14;
  snprintf(l3, sizeof(l3), "top : %s", top);
  canvas.drawString(l3, bx, y);
}

void viewGraph(int cpu, bool connected) {
  canvas.fillScreen(canvas.color565(16, 18, 24));
  canvas.setTextDatum(top_left);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextSize(2);
  canvas.drawString("CPU", 10, 8);
  canvas.setTextSize(1);
  char v[8]; snprintf(v, sizeof(v), "%d%%", cpu);
  canvas.setTextDatum(top_right);
  canvas.drawString(v, canvas.width() - 10, 14);

  int gx = 8, gy = 40, gw = canvas.width() - 16, gh = canvas.height() - 60;
  canvas.drawRect(gx, gy, gw, gh, canvas.color565(60, 64, 76));
  // grid lines at 25/50/75%
  for (int p = 25; p < 100; p += 25) {
    int yy = gy + gh - gh * p / 100;
    canvas.drawFastHLine(gx + 1, yy, gw - 2, canvas.color565(34, 38, 48));
  }
  // plot ring buffer oldest->newest
  portENTER_CRITICAL(&g_mux);
  for (int i = 1; i < HIST; i++) {
    int i0 = (g_histPos + i - 1) % HIST;
    int i1 = (g_histPos + i) % HIST;
    int x0 = gx + 1 + (gw - 2) * (i - 1) / (HIST - 1);
    int x1 = gx + 1 + (gw - 2) * (i) / (HIST - 1);
    int y0 = gy + gh - gh * g_hist[i0] / 100;
    int y1 = gy + gh - gh * g_hist[i1] / 100;
    canvas.drawLine(x0, y0, x1, y1, canvas.color565(245, 190, 70));
  }
  portEXIT_CRITICAL(&g_mux);
}

void viewProcs(int cpu, int ram, int temp, int net, int procs,
               const char* top, bool connected) {
  canvas.fillScreen(canvas.color565(16, 18, 24));
  renderTopBar(cpu, ram, temp, net, procs, top, connected);
  canvas.setTextSize(1);

  // --- TOP CPU ---
  canvas.setTextDatum(top_left);
  canvas.setTextColor(canvas.color565(245, 190, 70));
  canvas.drawString("TOP CPU", 8, 24);
  int y = 40;
  for (int i = 0; i < g_cpuProcN; i++) {
    canvas.setTextDatum(top_left);
    canvas.setTextColor(canvas.color565(210, 214, 224));
    canvas.drawString(g_cpuProcs[i].name, 8, y);
    char v[8]; snprintf(v, sizeof(v), "%d%%", g_cpuProcs[i].val);
    canvas.setTextDatum(top_right);
    canvas.drawString(v, canvas.width() - 8, y);
    y += 15;
  }
  if (g_cpuProcN == 0) {
    canvas.setTextColor(canvas.color565(120, 124, 138));
    canvas.drawString(connected ? "(waiting)" : "(no link)", 8, y);
  }

  // --- TOP RAM ---
  int ry = 122;
  canvas.drawFastHLine(8, ry - 6, canvas.width() - 16, canvas.color565(40, 44, 54));
  canvas.setTextDatum(top_left);
  canvas.setTextColor(canvas.color565(120, 200, 255));
  canvas.drawString("TOP RAM", 8, ry);
  y = ry + 16;
  for (int i = 0; i < g_ramProcN; i++) {
    canvas.setTextDatum(top_left);
    canvas.setTextColor(canvas.color565(210, 214, 224));
    canvas.drawString(g_ramProcs[i].name, 8, y);
    char v[8]; snprintf(v, sizeof(v), "%d%%", g_ramProcs[i].val);
    canvas.setTextDatum(top_right);
    canvas.drawString(v, canvas.width() - 8, y);
    y += 15;
  }
  if (g_ramProcN == 0) {
    canvas.setTextColor(canvas.color565(120, 124, 138));
    canvas.drawString(connected ? "(waiting)" : "(no link)", 8, y);
  }
}

// ENV screen (PCP-006): room temperature / humidity / pressure + trend.
void viewEnv(int cpu, int ram, int temp, int net, int procs,
             const char* top, bool connected) {
  canvas.fillScreen(canvas.color565(16, 18, 24));
  renderTopBar(cpu, ram, temp, net, procs, top, connected);

  canvas.setTextDatum(top_left);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextSize(2);
  canvas.drawString("ENV", 10, 22);

  int y = 52, bx = 10;
  canvas.setTextSize(1);
  auto row = [&](const char* lbl, const char* val, uint16_t col) {
    canvas.setTextDatum(top_left);
    canvas.setTextColor(col);
    canvas.drawString(lbl, bx, y);
    canvas.setTextColor(TFT_WHITE);
    canvas.drawString(val, bx + 56, y);
    y += 26;
  };

  char v[24];
  snprintf(v, sizeof(v), "%.1f C", g_envTemp);
  row("temp:",  v, canvas.color565(255, 150, 90));
  snprintf(v, sizeof(v), "%d %%", (int)(g_envHum + 0.5f));
  row("humid:", v, canvas.color565(120, 200, 255));
  int tr = pressTrend();
  const char* arrow = (tr > 0) ? "rising" : (tr < 0) ? "falling"
                    : (g_pressHistN < 3) ? "--" : "steady";
  snprintf(v, sizeof(v), "%d hPa %s", (int)(g_envPress + 0.5f), arrow);
  row("press:", v, canvas.color565(180, 220, 160));
}
