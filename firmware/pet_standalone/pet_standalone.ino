/*
 *  PC-Pet Standalone  --  educational single-file Tamagotchi
 *  ---------------------------------------------------------------
 *  A deliberately simple version for learning the concept. Unlike the
 *  full firmware it has NO BLE, NO HAT sensors, NO second task, and no
 *  mutex -- everything runs on one loop(). The pet's mood drifts on a
 *  random timer, and two buttons let you interact with it.
 *
 *  The big building blocks of any M5 sketch, each in its own section:
 *    1. CONFIG   -- tunable constants
 *    2. STATE    -- global variables that change over time
 *    3. HELPERS  -- pure logic (mood names, colours)
 *    4. RENDER   -- draw to an offscreen canvas, then push to the screen
 *    5. setup()  -- runs once at boot
 *    6. loop()   -- runs forever: read input -> update state -> draw
 *
 *  Controls:
 *    BtnA (front M5 button) : "play"  -> pet gets EXCITED, happiness up
 *    BtnB (side button)     : "feed"  -> pet gets HAPPY, hunger reset
 *    Power button           : hold ~1s to power off
 *
 *  Board: M5StickC Plus2.  Library: M5Unified only.
 * --------------------------------------------------------------- */

#include <M5Unified.h>

// =================  1. CONFIG  ================================
// One place for every tunable number, so behaviour is easy to change.

const uint32_t MOOD_MIN_MS   = 6000;    // pet picks a new random mood every
const uint32_t MOOD_MAX_MS   = 12000;   //   6-12 seconds (the "autonomous" feel)
const uint32_t REACT_MS      = 2500;    // a button reaction lasts this long
const uint32_t FRAME_MS      = 60;      // ~16 fps animation tick
const int      STAT_MAX      = 100;     // happiness / fullness ceiling
const uint32_t STAT_DECAY_MS = 4000;    // stats drop by 1 every this often

// =================  2. STATE  ================================
// Globals that change while the program runs. In a one-task sketch like
// this we can read/write them anywhere -- no locking needed.

// The set of moods. The M_ prefix avoids clashing with names the ESP32
// ROM headers already define (e.g. BUSY / HOT).
enum Mood { M_HAPPY, M_SAD, M_SLEEPY, M_HUNGRY, M_EXCITED, MOOD_COUNT };

Mood     g_mood       = M_HAPPY;   // what the pet feels right now
uint32_t g_nextMood   = 0;         // millis() when the mood will next drift
uint32_t g_reactUntil = 0;         // while > millis(), a button reaction holds
uint32_t g_lastFrame  = 0;         // millis() of the last animation frame
uint32_t g_lastDecay  = 0;         // millis() of the last stat decay
uint32_t g_frame      = 0;         // counts frames, drives the animation

int  g_happiness = 80;             // 0..STAT_MAX, raised by "play"
int  g_fullness  = 80;             // 0..STAT_MAX, raised by "feed"

M5Canvas canvas(&M5.Display);      // offscreen buffer we draw into

// =================  3. HELPERS  ==============================
// Small pure functions: given some input, return a value. No drawing.

const char* moodWord(Mood m) {
  switch (m) {
    case M_HAPPY:   return "happy";
    case M_SAD:     return "sad";
    case M_SLEEPY:  return "sleepy";
    case M_HUNGRY:  return "hungry";
    case M_EXCITED: return "yay!";
    default:        return "";
  }
}

uint16_t bodyColor(Mood m) {
  switch (m) {
    case M_HAPPY:   return canvas.color565(120, 215, 140);  // green
    case M_SAD:     return canvas.color565(120, 150, 230);  // blue
    case M_SLEEPY:  return canvas.color565(170, 150, 210);  // soft purple
    case M_HUNGRY:  return canvas.color565(240, 180,  90);  // amber
    case M_EXCITED: return canvas.color565(255, 140, 160);  // pink
    default:        return TFT_WHITE;
  }
}

// Pick a fresh mood, biased a little by the stats so the pet "feels" its
// needs: low fullness leans hungry, low happiness leans sad. Otherwise a
// plain random choice -- this is what makes the pet act on its own.
Mood rollMood() {
  if (g_fullness  < 30) return M_HUNGRY;
  if (g_happiness < 30) return M_SAD;
  return (Mood)random(MOOD_COUNT);
}

// =================  4. RENDER  ===============================
// Everything draws into `canvas` (offscreen), then loop() pushes the
// whole frame to the LCD at once. This avoids flicker.

void drawFace(int cx, int cy, Mood mood) {
  uint16_t col = bodyColor(mood);

  // gentle breathing: the body radius wobbles with a sine wave
  float t  = g_frame * 0.15f;
  int   r  = 46 + (int)(2.0f * sinf(t));

  // body + soft shadow
  canvas.fillEllipse(cx, cy + r + 6, r - 6, 4, canvas.color565(30, 32, 40));
  canvas.fillCircle(cx, cy, r, col);

  // eyes -- blink for a few frames every ~2 seconds
  bool blink = (g_frame % 32) < 3;
  int  ex = 16, ey = -8;
  if (mood == M_SLEEPY || blink) {
    // closed: a short horizontal line
    canvas.drawFastHLine(cx - ex - 6, cy + ey, 12, TFT_BLACK);
    canvas.drawFastHLine(cx + ex - 6, cy + ey, 12, TFT_BLACK);
  } else {
    canvas.fillCircle(cx - ex, cy + ey, 7, TFT_WHITE);
    canvas.fillCircle(cx + ex, cy + ey, 7, TFT_WHITE);
    int look = (mood == M_EXCITED) ? ((g_frame % 16 < 8) ? 2 : -2) : 0;
    canvas.fillCircle(cx - ex + look, cy + ey, 3, TFT_BLACK);
    canvas.fillCircle(cx + ex + look, cy + ey, 3, TFT_BLACK);
  }

  // mouth -- one shape per mood
  int my = cy + 16;
  switch (mood) {
    case M_HAPPY:
    case M_EXCITED:
      canvas.drawArc(cx, my - 4, 12, 9, 20, 160, TFT_BLACK); break;   // smile
    case M_SAD:
      canvas.drawArc(cx, my + 8, 12, 9, 200, 340, TFT_BLACK); break;  // frown
    case M_SLEEPY:
      canvas.fillEllipse(cx, my, 4, 3, canvas.color565(60, 60, 90)); break;
    case M_HUNGRY:
      canvas.fillCircle(cx, my, 4, TFT_BLACK); break;                 // little O
    default: break;
  }

  // a small per-mood effect
  if (mood == M_SLEEPY) {                         // floating Zzz
    canvas.setTextColor(canvas.color565(150, 170, 230));
    canvas.setTextDatum(middle_center);
    canvas.setTextSize(1);
    int zy = cy - r - 6 - (g_frame % 18);
    canvas.drawString("z", cx + r, zy);
    canvas.drawString("Z", cx + r + 8, zy - 8);
  } else if (mood == M_HUNGRY) {                  // sweat drop
    int dy = g_frame % 18;
    canvas.fillCircle(cx + r - 4, cy - 8 + dy, 3, canvas.color565(120, 200, 255));
  } else if (mood == M_EXCITED) {                 // sparkles
    canvas.fillCircle(cx - r, cy - r, 2, TFT_WHITE);
    canvas.fillCircle(cx + r, cy - r + 6, 2, TFT_WHITE);
  }
}

// A small labelled bar (used for happiness + fullness).
void drawBar(int x, int y, int w, const char* label, int pct, uint16_t col) {
  canvas.setTextDatum(middle_left);
  canvas.setTextColor(canvas.color565(170, 175, 190));
  canvas.setTextSize(1);
  canvas.drawString(label, x, y + 3);
  int bx = x + 20, bw = w - 20;
  canvas.drawRoundRect(bx, y, bw, 8, 2, canvas.color565(70, 70, 80));
  int fill = (bw - 2) * constrain(pct, 0, 100) / 100;
  canvas.fillRoundRect(bx + 1, y + 1, fill, 6, 2, col);
}

void render() {
  canvas.fillScreen(canvas.color565(16, 18, 24));

  // the one creature, centred
  drawFace(canvas.width() / 2, 84, g_mood);

  // mood word
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextSize(2);
  canvas.drawString(moodWord(g_mood), canvas.width() / 2, 150);

  // status bars
  drawBar(8, 188, canvas.width() - 16, "joy", g_happiness, canvas.color565(120, 215, 140));
  drawBar(8, 206, canvas.width() - 16, "fed", g_fullness,  canvas.color565(240, 180,  90));

  // button hints
  canvas.setTextDatum(middle_center);
  canvas.setTextColor(canvas.color565(120, 124, 138));
  canvas.setTextSize(1);
  canvas.drawString("A:play   B:feed", canvas.width() / 2, 228);

  canvas.pushSprite(0, 0);   // blit the finished frame to the LCD
}

// =================  5. setup  ================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(0);              // portrait 135 x 240
  M5.Display.setBrightness(110);
  M5.Speaker.begin();

  // seed the RNG with hardware noise so the mood sequence differs each boot
  randomSeed(esp_random());

  canvas.createSprite(M5.Display.width(), M5.Display.height());

  uint32_t now = millis();
  g_nextMood  = now + random(MOOD_MIN_MS, MOOD_MAX_MS);
  g_lastDecay = now;
}

// =================  6. loop  =================================
// The classic embedded pattern: read input -> update state -> draw.
void loop() {
  M5.update();                 // refresh button + power state
  uint32_t now = millis();

  // ---- read input ----
  if (M5.BtnA.wasClicked()) {          // "play"
    g_mood = M_EXCITED;
    g_reactUntil = now + REACT_MS;
    g_happiness = min(STAT_MAX, g_happiness + 15);
    M5.Speaker.tone(1800, 40);
  }
  if (M5.BtnB.wasClicked()) {          // "feed"
    g_mood = M_HAPPY;
    g_reactUntil = now + REACT_MS;
    g_fullness = min(STAT_MAX, g_fullness + 20);
    M5.Speaker.tone(1300, 40);
  }
  // (power-button hold to switch off is handled by M5 automatically)

  // ---- update state ----
  // stats slowly fall over time, giving the buttons a purpose
  if (now - g_lastDecay >= STAT_DECAY_MS) {
    g_lastDecay = now;
    if (g_happiness > 0) g_happiness--;
    if (g_fullness  > 0) g_fullness--;
  }

  // mood drifts on its own, unless a recent button press is still holding
  if (now >= g_reactUntil && now >= g_nextMood) {
    g_mood = rollMood();
    g_nextMood = now + random(MOOD_MIN_MS, MOOD_MAX_MS);
  }

  // ---- draw ----
  if (now - g_lastFrame >= FRAME_MS) {
    g_lastFrame = now;
    g_frame++;
    render();
  }
}
