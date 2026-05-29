/*
 *  PC-Pet  —  Tamagotchi for M5StickC Plus2
 *  -------------------------------------------------------------
 *  A virtual creature whose mood mirrors your computer's state.
 *  Metrics arrive over BLE (Nordic UART Service) from a Python
 *  host agent running on the PC (see pc_pet_agent.py).
 *
 *  Packet format (ASCII, comma-separated, one line):
 *      cpu,ram,temp,net,procs,topname,gpu,batt,charging;cpuList;ramList
 *
 *  Buttons:
 *      BtnA (front M5 button) : cycle view  Pet -> Stats -> Graph -> Procs
 *      BtnB (side button)     : single click = next character; double = mute
 *      Power button (lower L) : short = toggle screen; hold ~1s = power off
 *
 *  Board:  M5StickC Plus2   (ESP32-PICO-V3-02)
 *  Libs :  M5Unified, ESP32 BLE (bundled with arduino-esp32 core)
 * -------------------------------------------------------------- */

#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>

// ---- Nordic UART Service UUIDs -------------------------------
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // PC writes here
#define CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // device notify (spare)

#define DEVICE_NAME  "PCpet"

// ---- shared state (written by BLE task, read by main loop) ---
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile int  g_cpu = 0, g_ram = 0, g_temp = -1, g_net = 0, g_procs = 0;
volatile int  g_gpu = -1, g_batt = -1, g_charging = 0;   // GPU %, PC battery %, charging
char          g_top[16] = "-";
volatile bool g_connected = false;
volatile uint32_t g_lastPacket = 0;

// top processes (written + read on the main task -> no mutex needed)
#define NPROC 4
struct ProcEntry { char name[14]; int val; };
ProcEntry g_cpuProcs[NPROC];
int       g_cpuProcN = 0;
ProcEntry g_ramProcs[NPROC];
int       g_ramProcN = 0;

// CPU history for the graph view (ring buffer)
#define HIST 110
uint8_t g_hist[HIST];
int     g_histPos = 0;

// ---- pet moods ----------------------------------------------
enum Mood { M_SLEEP, M_HAPPY, M_BUSY, M_STUFFED, M_HOT, M_PANIC, M_LOWPWR };
const char* moodWord(Mood m) {
  switch (m) {
    case M_SLEEP:   return "zzz...";
    case M_HAPPY:   return "chillin";
    case M_BUSY:    return "workin'";
    case M_STUFFED: return "so full";
    case M_HOT:     return "too hot!";
    case M_PANIC:   return "PANIC!!";
    case M_LOWPWR:  return "low pwr";
  }
  return "";
}

// ---- views ---------------------------------------------------
enum View { VIEW_PET, VIEW_STATS, VIEW_GRAPH, VIEW_PROCS, VIEW_COUNT };
int  g_view = VIEW_PET;
bool g_mute = false;

// ---- characters (cycled with a single click on BtnB) ----
const int CHAR_COUNT = 5;
int g_char = 0;
const char* charName(int c) {
  switch (c) {
    case 1: return "Cat";
    case 2: return "Robo";
    case 3: return "Ghost";
    case 4: return "Bunny";
  }
  return "Blobby";
}

// ---- screen power management ----
const uint8_t  FULL_BRI     = 110;     // active brightness
const uint8_t  DIM_BRI      = 55;      // ~50% after some idle
const uint32_t DIM_AFTER_MS = 10000;   // 10 s -> dim
const uint32_t OFF_AFTER_MS = 30000;   // 30 s -> backlight off
const float    SHAKE_THRESH  = 1.2f;   // per-sample accel delta to count as shaking
const uint32_t SHAKE_HOLD_MS = 750;    // must keep shaking ~0.75 s to wake
uint32_t g_lastActivity = 0;
int      g_curBri = -1;
float    g_lax = 0, g_lay = 0, g_laz = 0;
bool     g_accelInit = false;
bool     g_forceOff  = false;   // power-button short press forced the screen off
uint32_t g_shakeStart = 0;      // when the current shake burst began (0 = none)
uint32_t g_lastShake  = 0;      // last time a strong-shake sample was seen

// ---- low-battery alert ----
const int      LOW_BATT_PCT    = 10;
const uint32_t LOW_BATT_REPEAT = 120000;   // re-beep every 2 min while low
bool     g_battWasLow  = false;
uint32_t g_lastBattBeep = 0;

// ---- 1 Hz tick (PCP-001) ----
uint32_t g_lastTick1s = 0;
uint32_t g_uptimeSec  = 0;
uint32_t g_panicSec   = 0;

M5Canvas canvas(&M5.Display);

// =================  BLE callbacks  ============================
// The BLE callback runs on the Bluetooth task, which has a small stack,
// so we keep it tiny: just stash the raw bytes. loop() parses them.
volatile bool   g_rxReady = false;
uint8_t         g_rxBuf[200];
volatile size_t g_rxLen   = 0;

// debug counters (written in callback, printed in loop)
volatile uint32_t g_writeCount  = 0;
volatile size_t   g_dbgGetLen   = 0;
volatile size_t   g_dbgParamLen = 0;

static void stashBytes(const uint8_t* data, size_t len) {
  if (!data || len == 0) return;
  if (len > sizeof(g_rxBuf) - 1) len = sizeof(g_rxBuf) - 1;
  portENTER_CRITICAL(&g_mux);
  memcpy(g_rxBuf, data, len);
  g_rxLen = len;
  g_rxReady = true;
  portEXIT_CRITICAL(&g_mux);
}

class RxCallbacks : public BLECharacteristicCallbacks {
  // Newer arduino-esp32 (Bluedroid) calls this two-arg overload.
#if defined(CONFIG_BLUEDROID_ENABLED)
  void onWrite(BLECharacteristic* c, esp_ble_gatts_cb_param_t* param) override {
    size_t gl = c->getLength();
    size_t pl = (param) ? param->write.len : 0;
    g_writeCount++; g_dbgGetLen = gl; g_dbgParamLen = pl;
    if (gl > 0) stashBytes(c->getData(), gl);
    else if (param && param->write.len && param->write.value)
      stashBytes(param->write.value, param->write.len);
  }
#endif
  // Fallback one-arg overload (older cores / NimBLE backend).
  void onWrite(BLECharacteristic* c) override {
    size_t gl = c->getLength();
    g_writeCount++; g_dbgGetLen = gl; g_dbgParamLen = 0;
    stashBytes(c->getData(), gl);
  }
};

// Parse a "name:val,name:val,..." list into an array. Returns count.
static int parseProcList(char* s, ProcEntry* arr) {
  int n = 0;
  char* save = nullptr;
  char* tok = strtok_r(s, ",", &save);
  while (tok && n < NPROC) {
    char* colon = strchr(tok, ':');
    if (colon) {
      *colon = '\0';
      strncpy(arr[n].name, tok, sizeof(arr[n].name) - 1);
      arr[n].name[sizeof(arr[n].name) - 1] = '\0';
      arr[n].val = atoi(colon + 1);
      n++;
    }
    tok = strtok_r(nullptr, ",", &save);
  }
  return n;
}

// Parse "cpu,ram,temp,net,procs,topname;cpuList;ramList" and update state.
// Runs on the main task (from loop), so printf + buffers are safe here.
static void parsePacket(char* buf) {
  // split top-level sections by ';'
  char* save1 = nullptr;
  char* metricsPart = strtok_r(buf, ";", &save1);
  char* cpuPart     = strtok_r(nullptr, ";", &save1);
  char* ramPart     = strtok_r(nullptr, ";", &save1);

  int   cpu = 0, ram = 0, temp = -1, net = 0, procs = 0;
  int   gpu = -1, batt = -1, charging = 0;
  char  top[16] = "-";
  if (metricsPart) {
    char* save2 = nullptr;
    char* tok   = strtok_r(metricsPart, ",", &save2);
    int   idx   = 0;
    while (tok) {
      switch (idx) {
        case 0: cpu      = atoi(tok); break;
        case 1: ram      = atoi(tok); break;
        case 2: temp     = atoi(tok); break;
        case 3: net      = atoi(tok); break;
        case 4: procs    = atoi(tok); break;
        case 5: strncpy(top, tok, 15); top[15] = '\0'; break;
        case 6: gpu      = atoi(tok); break;
        case 7: batt     = atoi(tok); break;
        case 8: charging = atoi(tok); break;
      }
      tok = strtok_r(nullptr, ",", &save2);
      idx++;
    }
  }

  ProcEntry cpuTmp[NPROC], ramTmp[NPROC];
  int cpuN = cpuPart ? parseProcList(cpuPart, cpuTmp) : 0;
  int ramN = ramPart ? parseProcList(ramPart, ramTmp) : 0;

  portENTER_CRITICAL(&g_mux);
  g_cpu = constrain(cpu, 0, 100);
  g_ram = constrain(ram, 0, 100);
  g_temp = temp;
  g_net = net;
  g_procs = procs;
  g_gpu = (gpu < 0) ? -1 : constrain(gpu, 0, 100);
  g_batt = (batt < 0) ? -1 : constrain(batt, 0, 100);
  g_charging = charging;
  strncpy(g_top, top, 15);
  g_top[15] = '\0';
  g_lastPacket = millis();
  g_connected = true;
  g_hist[g_histPos] = (uint8_t)g_cpu;
  g_histPos = (g_histPos + 1) % HIST;
  for (int i = 0; i < cpuN; i++) g_cpuProcs[i] = cpuTmp[i];
  g_cpuProcN = cpuN;
  for (int i = 0; i < ramN; i++) g_ramProcs[i] = ramTmp[i];
  g_ramProcN = ramN;
  portEXIT_CRITICAL(&g_mux);
}

class ServerCallbacks : public BLEServerCallbacks {
  void onConnect(BLEServer* s) override {
    Serial.println("BLE: central connected");
  }
  void onDisconnect(BLEServer* s) override {
    Serial.println("BLE: central disconnected, re-advertising");
    g_connected = false;
    BLEDevice::startAdvertising();  // allow re-connection
  }
};

// =================  setup  ====================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\nPC-Pet booting...");
  M5.Display.setRotation(0);          // portrait 135 x 240
  M5.Display.setBrightness(110);
  M5.Speaker.begin();
  M5.Speaker.setVolume(120);
  g_lastActivity = millis();

  for (int i = 0; i < HIST; i++) g_hist[i] = 0;

  if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
    // fall back to 8-bit colour depth if 16-bit won't fit in RAM
    canvas.setColorDepth(8);
    canvas.createSprite(M5.Display.width(), M5.Display.height());
  }

  // splash
  M5.Display.fillScreen(TFT_BLACK);
  M5.Display.setTextColor(TFT_WHITE);
  M5.Display.setTextDatum(middle_center);
  M5.Display.setTextSize(2);
  M5.Display.drawString("PC-Pet", M5.Display.width() / 2, 90);
  M5.Display.setTextSize(1);
  M5.Display.drawString("waiting for PC...", M5.Display.width() / 2, 130);

  // ---- BLE peripheral ----
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setMTU(185);
  BLEServer* server = BLEDevice::createServer();
  server->setCallbacks(new ServerCallbacks());

  BLEService* svc = server->createService(SERVICE_UUID);

  BLECharacteristic* rx = svc->createCharacteristic(
      CHAR_RX_UUID,
      BLECharacteristic::PROPERTY_WRITE | BLECharacteristic::PROPERTY_WRITE_NR);
  rx->setCallbacks(new RxCallbacks());

  BLECharacteristic* tx = svc->createCharacteristic(
      CHAR_TX_UUID, BLECharacteristic::PROPERTY_NOTIFY);
  tx->addDescriptor(new BLE2902());

  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();

  // Put the device name directly in the PRIMARY advertising packet so it is
  // reliably discoverable by name (especially on macOS CoreBluetooth).
  // Keep the 128-bit service UUID in the scan response so the 31-byte main
  // packet doesn't overflow (which silently drops the whole payload).
  BLEAdvertisementData advData;
  advData.setFlags(0x06);                 // LE General Discoverable + BR/EDR not supported
  advData.setName(DEVICE_NAME);
  adv->setAdvertisementData(advData);

  BLEAdvertisementData scanResp;
  scanResp.setCompleteServices(BLEUUID(SERVICE_UUID));
  adv->setScanResponseData(scanResp);

  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}

// =================  helpers  ==================================
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

// draw a small horizontal bar
void drawBar(int x, int y, int w, int h, int pct, uint16_t col) {
  canvas.drawRoundRect(x, y, w, h, 2, canvas.color565(70, 70, 80));
  int fill = (w - 2) * constrain(pct, 0, 100) / 100;
  canvas.fillRoundRect(x + 1, y + 1, fill, h - 2, 2, col);
}

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
  uint16_t col = bodyColor(mood);

  // motion per mood
  float breathe = 1.0f + 0.05f * sinf(t);      // idle breathing
  int   jitter  = 0;                            // panic shake
  int   bounceY = 0;
  if (mood == M_PANIC) jitter = (frame % 2) ? 2 : -2;
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
  } else {
    int wide = (mood == M_PANIC || mood == M_HOT) ? 2 : 0;   // wide eyes when stressed
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
      canvas.fillEllipse(cx, my + 2, 7, 9, TFT_BLACK);
      canvas.fillEllipse(cx, my + 4, 4, 4, canvas.color565(200, 60, 60)); break;
  }

  // sweat drops when hot / panic
  if (mood == M_HOT || mood == M_PANIC) {
    int dy = (frame % 20);
    canvas.fillCircle(cx + rx - 2, cy - 6 + dy, 3, canvas.color565(120, 200, 255));
    canvas.fillCircle(cx - rx + 1, cy - 2 + (dy + 8) % 20, 2,
                      canvas.color565(120, 200, 255));
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
  canvas.drawString(connected ? moodWord(mood) : "waiting", canvas.width() / 2, 150);

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
}

void viewStats(int cpu, int ram, int temp, int net, int procs,
               const char* top, int gpu, int batt, int charging, bool connected) {
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

// =================  main loop  ================================
uint32_t g_frame = 0;
Mood     g_prevMood = M_HAPPY;

void loop() {
  M5.update();

  // ---- wake on a sustained strong shake ----
  {
    float ax, ay, az;
    if (M5.Imu.getAccel(&ax, &ay, &az)) {
      if (g_accelInit) {
        float d = fabsf(ax - g_lax) + fabsf(ay - g_lay) + fabsf(az - g_laz);
        uint32_t now = millis();
        if (d > SHAKE_THRESH) {
          // (re)start the burst if this is a fresh shake after a gap
          if (g_shakeStart == 0 || now - g_lastShake > 400) g_shakeStart = now;
          g_lastShake = now;
          if (now - g_shakeStart >= SHAKE_HOLD_MS) {   // shaken long enough -> wake
            g_lastActivity = now;
            g_forceOff = false;
            g_shakeStart = 0;
          }
        } else if (now - g_lastShake > 400) {
          g_shakeStart = 0;                            // shaking stopped -> reset
        }
      }
      g_lax = ax; g_lay = ay; g_laz = az;
      g_accelInit = true;
    }
  }

  // ---- drain any received BLE packet (parsed here, not in the callback) ----
  if (g_rxReady) {
    char local[200];
    size_t llen;
    portENTER_CRITICAL(&g_mux);
    llen = g_rxLen;
    if (llen > sizeof(local) - 1) llen = sizeof(local) - 1;
    memcpy(local, (const void*)g_rxBuf, llen);
    g_rxReady = false;
    portEXIT_CRITICAL(&g_mux);
    local[llen] = '\0';
    Serial.printf("RX(%u): %s\n", (unsigned)llen, local);
    parsePacket(local);
  }

  // ---- 1 Hz debug heartbeat: are writes arriving? ----
  static uint32_t lastDbg = 0;
  if (millis() - lastDbg > 1000) {
    lastDbg = millis();
    Serial.printf("dbg: writes=%lu getLen=%u paramLen=%u conn=%d\n",
                  (unsigned long)g_writeCount, (unsigned)g_dbgGetLen,
                  (unsigned)g_dbgParamLen, (int)g_connected);
  }

  // ---- buttons ----
  if (M5.BtnA.wasClicked()) {
    g_lastActivity = millis(); g_forceOff = false;
    g_view = (g_view + 1) % VIEW_COUNT;
    M5.Speaker.tone(1500, 30);
  }
  if (M5.BtnB.wasSingleClicked()) {
    g_lastActivity = millis(); g_forceOff = false;
    g_char = (g_char + 1) % CHAR_COUNT;
    M5.Speaker.tone(1700, 30);
  }
  if (M5.BtnB.wasDoubleClicked()) {
    g_lastActivity = millis(); g_forceOff = false;
    g_mute = !g_mute;
    M5.Speaker.tone(g_mute ? 600 : 1800, 40);
  }
  // power button: short press toggles screen off/on; long hold -> power off
  if (M5.BtnPWR.wasClicked()) {
    g_forceOff = !g_forceOff;
    if (!g_forceOff) g_lastActivity = millis();   // turning back on resets idle timer
  }
  if (M5.BtnPWR.pressedFor(1000)) {
    M5.Power.powerOff();
  }

  // ---- snapshot shared state ----
  int cpu, ram, temp, net, procs;
  int gpu, pcbatt, pcchg;
  char top[16];
  bool connected;
  uint32_t last;
  portENTER_CRITICAL(&g_mux);
  cpu = g_cpu; ram = g_ram; temp = g_temp; net = g_net; procs = g_procs;
  gpu = g_gpu; pcbatt = g_batt; pcchg = g_charging;
  strncpy(top, g_top, 15); top[15] = '\0';
  connected = g_connected; last = g_lastPacket;
  portEXIT_CRITICAL(&g_mux);

  // stale link -> treat as disconnected after 6 s
  if (connected && millis() - last > 6000) connected = false;

  // ---- alert beeps on mood escalation ----
  Mood mood = connected ? currentMood(cpu, ram, temp, gpu, pcbatt, pcchg) : M_SLEEP;
  if (!g_mute && connected && mood != g_prevMood &&
      (mood == M_PANIC || mood == M_HOT)) {
    M5.Speaker.tone(2300, 90);
    delay(110);
    M5.Speaker.tone(2300, 90);
  }
  g_prevMood = mood;

  // ---- 1 Hz tick ----
  if (millis() - g_lastTick1s >= 1000) {
    g_lastTick1s = millis();
    g_uptimeSec++;
    if (mood == M_PANIC)
      g_panicSec++;
    else
      g_panicSec = 0;
  }

  // ---- screen power management ----
  // Stay fully awake while the pet is in an alert state.
  if (mood == M_HOT || mood == M_PANIC) { g_lastActivity = millis(); g_forceOff = false; }
  uint32_t idle = millis() - g_lastActivity;
  uint8_t  targetBri = FULL_BRI;
  bool     screenOn  = true;
  if (g_forceOff || idle >= OFF_AFTER_MS) { targetBri = 0;       screenOn = false; }
  else if (idle >= DIM_AFTER_MS)          { targetBri = DIM_BRI; screenOn = true;  }
  if ((int)targetBri != g_curBri) {
    M5.Display.setBrightness(targetBri);
    g_curBri = targetBri;
  }

  // ---- low-battery alert ----
  int batt = M5.Power.getBatteryLevel();
  bool battLow = (batt >= 0 && batt < LOW_BATT_PCT);
  if (battLow) {
    bool justCrossed = !g_battWasLow;                       // first time dropping below
    bool dueAgain = (millis() - g_lastBattBeep > LOW_BATT_REPEAT);
    if (justCrossed || dueAgain) {
      g_lastBattBeep = millis();
      g_lastActivity = millis();                            // wake screen to show warning
      if (!g_mute) {
        M5.Speaker.tone(1200, 120); delay(140);
        M5.Speaker.tone(900, 200);
      }
    }
  }
  g_battWasLow = battLow;

  // ---- render selected view (skipped while the screen is off) ----
  if (screenOn) {
    switch (g_view) {
      case VIEW_PET:   viewPet(cpu, ram, temp, net, procs, top, gpu, pcbatt, pcchg, connected, g_frame); break;
      case VIEW_STATS: viewStats(cpu, ram, temp, net, procs, top, gpu, pcbatt, pcchg, connected);        break;
      case VIEW_GRAPH: viewGraph(cpu, connected);                                     break;
      case VIEW_PROCS: viewProcs(cpu, ram, temp, net, procs, top, connected);         break;
    }
    canvas.pushSprite(0, 0);
  }

  g_frame++;
  delay(screenOn ? 55 : 150);   // ~18 fps awake, slower when display is off
}
