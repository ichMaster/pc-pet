/*
 *  PC-Pet FSM  --  educational rule-based state machine
 *  ---------------------------------------------------------------
 *  Shows ONE idea: a unified Tamagotchi state machine whose transitions
 *  are configured by a table of RULES, instead of scattered if/else.
 *
 *  A classical finite state machine has four parts:
 *    1. STATES      -- the fixed set the pet can be in (enum State)
 *    2. EVENTS      -- inputs that can cause a change (here: a 1 Hz tick,
 *                      button presses, and the decaying stats)
 *    3. TRANSITIONS -- rules: "from THIS state, IF condition, go to THAT"
 *    4. ACTIONS     -- what runs when a state is entered (onEnter)
 *
 *  The whole machine is the RULES[] table below + the tiny step() that
 *  walks it. Change behaviour by editing the table, not the code.
 *
 *  No BLE, no HAT, no PC. One screen, one character. M5Unified only.
 *  Board: M5StickC Plus2.
 * --------------------------------------------------------------- */

#include <M5Unified.h>

// =================  1. STATES  ===============================
enum State { S_SLEEP, S_HAPPY, S_HUNGRY, S_PLAYING, S_PANIC, STATE_COUNT };

const char* stateName(State s) {
  switch (s) {
    case S_SLEEP:   return "zzz";
    case S_HAPPY:   return "happy";
    case S_HUNGRY:  return "hungry";
    case S_PLAYING: return "play!";
    case S_PANIC:   return "PANIC";
    default:        return "?";
  }
}

uint16_t stateColor(State s) {
  switch (s) {
    case S_SLEEP:   return M5.Display.color565(120, 150, 230);
    case S_HAPPY:   return M5.Display.color565(120, 215, 140);
    case S_HUNGRY:  return M5.Display.color565(240, 180,  90);
    case S_PLAYING: return M5.Display.color565(255, 140, 160);
    case S_PANIC:   return M5.Display.color565(245,  90,  80);
    default:        return TFT_WHITE;
  }
}

// =================  2. STATE VARIABLES  ======================
// The machine's memory: the current state + a few inputs the rules read.
State    g_state    = S_HAPPY;
int      g_hunger   = 0;      // 0 (full) .. 100 (starving) -- rises over time
int      g_energy   = 100;    // 100 (rested) .. 0 (exhausted) -- falls over time
bool     g_btnFeed  = false;  // set by BtnB this tick (an EVENT)
bool     g_btnPlay  = false;  // set by BtnA this tick (an EVENT)
uint32_t g_inState  = 0;      // millis() when we entered the current state

uint32_t g_lastTick = 0;      // 1 Hz tick timer
uint32_t g_frame    = 0;
M5Canvas canvas(&M5.Display);

// =================  3. THE RULE TABLE (the machine)  =========
// Each rule: "if we are in `from` AND cond() is true, switch to `to`."
// ANY_STATE means the rule applies no matter the current state.
// Rules are checked top-to-bottom; the FIRST match wins, so ORDER = priority.
// This single table IS the entire transition logic.

const int ANY_STATE = -1;

struct Rule {
  int    from;          // a State, or ANY_STATE
  bool (*cond)();       // guard: returns true when this transition should fire
  State  to;            // destination state
  const char* why;      // human-readable label (for the on-screen log)
};

// ---- guard conditions (the "rules", as plain functions) ----
bool cFeedPressed() { return g_btnFeed; }
bool cPlayPressed() { return g_btnPlay; }
bool cStarving()    { return g_hunger >= 70; }
bool cExhausted()   { return g_energy <= 15; }
bool cRested()      { return g_energy >= 90; }
bool cPlayDone()    { return millis() - g_inState > 3000; }   // a timed transition
bool cCalm()        { return g_hunger < 70 && g_energy > 15; }

// Highest-priority rules first. Buttons beat needs; needs beat "calm".
const Rule RULES[] = {
  // from        condition       to          why
  { ANY_STATE,   cExhausted,     S_SLEEP,    "no energy" },
  { ANY_STATE,   cStarving,      S_HUNGRY,   "very hungry" },
  { ANY_STATE,   cPlayPressed,   S_PLAYING,  "BtnA play" },
  { ANY_STATE,   cFeedPressed,   S_HAPPY,    "BtnB fed" },
  { S_SLEEP,     cRested,        S_HAPPY,    "rested" },
  { S_PLAYING,   cPlayDone,      S_HAPPY,    "played out" },
  { S_HUNGRY,    cFeedPressed,   S_HAPPY,    "fed" },
};
const int RULE_COUNT = sizeof(RULES) / sizeof(RULES[0]);

const char* g_lastWhy = "boot";   // why we last transitioned (shown on screen)

// =================  4. ACTIONS (onEnter)  ===================
// Runs once when a state becomes active. Side effects live here, not in
// the rules -- rules only decide, actions only do.
void onEnter(State s) {
  g_inState = millis();
  switch (s) {
    case S_PLAYING: g_energy = max(0, g_energy - 20);
                    M5.Speaker.tone(1800, 40); break;
    case S_HAPPY:   g_hunger = max(0, g_hunger - 40);   // feeding fills tummy
                    M5.Speaker.tone(1300, 40); break;
    case S_PANIC:   M5.Speaker.tone(2300, 90); break;
    case S_SLEEP:   break;   // energy recovers passively in the tick
    default:        break;
  }
}

// The engine: find the first matching rule and switch. This is the whole
// "state machine" -- generic, driven entirely by RULES[].
void step() {
  for (int i = 0; i < RULE_COUNT; i++) {
    const Rule& r = RULES[i];
    if (r.from != ANY_STATE && r.from != (int)g_state) continue;  // wrong state
    if (r.to == g_state) continue;                                // no self-loop
    if (r.cond()) {                                               // guard passes?
      g_state   = r.to;
      g_lastWhy = r.why;
      onEnter(g_state);
      return;                 // first match wins
    }
  }
}

// =================  5. RENDER  ==============================
void render() {
  canvas.fillScreen(canvas.color565(16, 18, 24));
  uint16_t col = stateColor(g_state);

  // simple breathing face
  int cx = canvas.width() / 2, cy = 70;
  int r = 42 + (int)(2 * sinf(g_frame * 0.15f));
  canvas.fillCircle(cx, cy, r, col);
  bool blink = (g_frame % 30) < 3 || g_state == S_SLEEP;
  if (blink) {
    canvas.drawFastHLine(cx - 22, cy - 8, 12, TFT_BLACK);
    canvas.drawFastHLine(cx + 10, cy - 8, 12, TFT_BLACK);
  } else {
    canvas.fillCircle(cx - 16, cy - 8, 4, TFT_BLACK);
    canvas.fillCircle(cx + 16, cy - 8, 4, TFT_BLACK);
  }

  canvas.setTextDatum(middle_center);
  canvas.setTextColor(TFT_WHITE);
  canvas.setTextSize(2);
  canvas.drawString(stateName(g_state), cx, 132);

  // show WHY the machine is here -- makes the rules visible while learning
  canvas.setTextSize(1);
  canvas.setTextColor(canvas.color565(150, 155, 170));
  canvas.drawString(g_lastWhy, cx, 154);

  // stat bars
  auto bar = [&](int y, const char* lbl, int v, uint16_t c) {
    canvas.setTextDatum(middle_left);
    canvas.drawString(lbl, 8, y + 3);
    canvas.drawRoundRect(40, y, canvas.width() - 48, 8, 2, canvas.color565(70, 70, 80));
    canvas.fillRoundRect(41, y + 1, (canvas.width() - 50) * constrain(v, 0, 100) / 100,
                         6, 2, c);
  };
  bar(180, "hun", g_hunger, canvas.color565(240, 180, 90));
  bar(196, "enr", g_energy, canvas.color565(120, 215, 140));

  canvas.setTextDatum(middle_center);
  canvas.setTextColor(canvas.color565(120, 124, 138));
  canvas.drawString("A:play  B:feed", cx, 224);
  canvas.pushSprite(0, 0);
}

// =================  6. setup / loop  ========================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  M5.Display.setRotation(0);
  M5.Display.setBrightness(110);
  M5.Speaker.begin();
  canvas.createSprite(M5.Display.width(), M5.Display.height());
  g_inState = millis();
}

void loop() {
  M5.update();
  uint32_t now = millis();

  // --- collect EVENTS for this tick ---
  g_btnPlay = M5.BtnA.wasClicked();
  g_btnFeed = M5.BtnB.wasClicked();

  // --- 1 Hz tick: stats drift (this is what makes needs fire over time) ---
  if (now - g_lastTick >= 1000) {
    g_lastTick = now;
    g_hunger = min(100, g_hunger + 3);
    if (g_state == S_SLEEP) g_energy = min(100, g_energy + 8);  // rest recovers
    else                    g_energy = max(0, g_energy - 2);
  }

  // --- run the state machine: rules in, maybe a transition out ---
  step();

  // --- draw ~16 fps ---
  g_frame++;
  render();
  delay(60);
}
