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
#include "M5UnitENV.h"   // ENV III HAT (SHT30 + QMP6988), PCP-005
#include "pet_types.h"   // shared enums/structs (visible to auto-prototypes)

// ---- Nordic UART Service UUIDs -------------------------------
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // PC writes here
#define CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // device notify (spare)

#define DEVICE_NAME  "PCpet"

// ---- HAT selection (PCP-012) -------------------------------------
// SPK2 (MAX98357 I2S amp) and ENV III share the top HAT port, and SPK2 has no
// I2C address, so it cannot be auto-detected. Pick the fitted HAT here and
// reflash when you swap HATs:
//   HAT_SELECT = -1        auto-probe ENV III; no HAT if absent (never SPK2)
//   HAT_SELECT = HAT_ENV   same as auto (probe ENV III)
//   HAT_SELECT = HAT_SPK2  force the SPK2 I2S amp (skips the ENV probe)
#define HAT_SELECT (-1)


// ---- shared state (written by BLE task, read by main loop) ---
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile int  g_cpu = 0, g_ram = 0, g_temp = -1, g_net = 0, g_procs = 0;
volatile int  g_gpu = -1, g_batt = -1, g_charging = 0;   // GPU %, PC battery %, charging
volatile int  g_diskR = 0, g_diskW = 0;
char          g_top[16] = "-";
volatile bool g_connected = false;
volatile uint32_t g_lastPacket = 0;
BLECharacteristic* g_txChar = nullptr;   // device -> PC notify (PCP-009)

// top processes (written + read on the main task -> no mutex needed)
#define NPROC 4   // ProcEntry is defined in pet_types.h
ProcEntry g_cpuProcs[NPROC];
int       g_cpuProcN = 0;
ProcEntry g_ramProcs[NPROC];
int       g_ramProcN = 0;

// CPU history for the graph view (ring buffer)
#define HIST 110
uint8_t g_hist[HIST];
int     g_histPos = 0;

// ---- pet moods (Mood enum is in pet_types.h) ----------------
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

// ---- views (View enum is in pet_types.h) --------------------
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

// ---- ENV III HAT (PCP-005): SHT30 + QMP6988 on Wire1(0,26) ----
SHT3X    g_sht30;
QMP6988  g_qmp6988;
bool     g_envPresent = false;
float    g_envTemp    = 0;    // room temperature, Celsius
float    g_envHum     = 0;    // relative humidity, %
float    g_envPress   = 0;    // barometric pressure, hPa
uint32_t g_lastEnvRead = 0;

// ---- HAT mode resolved at boot (PCP-012) ----
HatMode  g_hat = HAT_NONE;

// ---- pressure trend (PCP-007): slow ring buffer for a barometer ----
const int      PRESS_HIST = 60;          // samples kept
float          g_pressHist[PRESS_HIST];
int            g_pressHistN = 0;         // number of valid samples
int            g_pressHistPos = 0;       // next write index
uint32_t       g_lastPressLog = 0;
const uint32_t PRESS_LOG_MS = 60000;     // log once per minute

// ---- ENV telemetry to PC (PCP-009) ----
uint32_t       g_lastEnvSend = 0;
const uint32_t ENV_SEND_MS = 5000;       // notify every ~5 s when connected

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
  int   gpu = -1, batt = -1, charging = 0, diskR = 0, diskW = 0;
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
        case 8:  charging = atoi(tok); break;
        case 9:  diskR    = atoi(tok); break;
        case 10: diskW    = atoi(tok); break;
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
  g_diskR = constrain(diskR, 0, 9999);
  g_diskW = constrain(diskW, 0, 9999);
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

  // ---- audio routing (PCP-012) ----
  // For SPK2, point M5.Speaker at the HAT's I2S pins BEFORE begin(); otherwise
  // the built-in speaker config is left untouched. NOTE: M5Unified's
  // speaker_config_t field names vary by version -- verify on-device.
  if ((int)HAT_SELECT == HAT_SPK2) {
    auto spc = M5.Speaker.config();
    spc.pin_data_out = 25;   // SPK2 DOUT = G25
    spc.pin_bck      = 26;   // SPK2 BCLK = G26
    spc.pin_ws       = 0;    // SPK2 LRC  = G0
    M5.Speaker.config(spc);
    g_hat = HAT_SPK2;
  }
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

  // ---- ENV III HAT (PCP-005) ----
  // Separate I2C bus (Wire1) on the HAT pins G0=SDA / G26=SCL so it does not
  // clash with the internal IMU/RTC on the main Wire bus. Presence-checked:
  // G0 is a strap pin, so a missing/again HAT must not hang boot.
  // ENV III HAT on the main Wire bus (G0=SDA / G26=SCL). Skip the probe in SPK2
  // mode -- there G0/G26 are the amp's I2S pins, not I2C.
  if (g_hat != HAT_SPK2) {
    // begin() returns true even when absent, so confirm with a real read.
    g_sht30.begin(&Wire, SHT3X_I2C_ADDR, 0, 26, 400000U);
    g_qmp6988.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, 0, 26, 400000U);
    g_envPresent = g_sht30.update() && g_qmp6988.update();
    if (g_envPresent) g_hat = HAT_ENV;
  }
  Serial.printf("HAT: %s\n", g_hat == HAT_SPK2 ? "SPK2"
                           : g_hat == HAT_ENV  ? "ENV III" : "none");

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
  g_txChar = tx;   // PCP-009: keep a handle for ENV notify from loop()

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

// ---- melody system (PCP-002) ----
const MelNote MEL_ALERT[]      = { {2300, 90, 20}, {2300, 90, 0}, {0,0,0} };
const MelNote MEL_PANIC1[]     = { {2000, 100, 50}, {2400, 100, 0}, {0,0,0} };
const MelNote MEL_PANIC2[]     = { {2000, 80, 40}, {2400, 80, 40}, {2000, 80, 40}, {2400, 80, 0}, {0,0,0} };
const MelNote MEL_PANIC3[]     = { {2600, 60, 30}, {2200, 60, 30}, {2600, 60, 30}, {2200, 60, 30}, {2800, 120, 0}, {0,0,0} };
const MelNote MEL_LOWPWR[]     = { {1200, 120, 20}, {900, 200, 0}, {0,0,0} };
const MelNote MEL_LOWBATT[]    = { {800, 150, 30}, {600, 200, 0}, {0,0,0} };
const MelNote MEL_CLICK[]      = { {1500, 30, 0}, {0,0,0} };
const MelNote MEL_CHARSWITCH[] = { {1700, 30, 0}, {0,0,0} };
const MelNote MEL_MUTE_ON[]    = { {600, 40, 0}, {0,0,0} };
const MelNote MEL_MUTE_OFF[]   = { {1800, 40, 0}, {0,0,0} };

// =================  helpers / rendering  ======================
// Logic helpers (playMelody, currentMood, colours, panic tiers, pressure
// trend, ENV modifier, drawBar) now live in pet_helpers.ino.
// Character art and all view screens live in pet_render.ino.
// The Arduino IDE concatenates those tabs onto this sketch automatically.

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
    // ENV screen (PCP-006) is only in the cycle when the HAT is present
    if (g_view == VIEW_ENV && !g_envPresent) g_view = (g_view + 1) % VIEW_COUNT;
    playMelody(MEL_CLICK);
  }
  if (M5.BtnB.wasSingleClicked()) {
    g_lastActivity = millis(); g_forceOff = false;
    g_char = (g_char + 1) % CHAR_COUNT;
    playMelody(MEL_CHARSWITCH);
  }
  if (M5.BtnB.wasDoubleClicked()) {
    g_lastActivity = millis(); g_forceOff = false;
    g_mute = !g_mute;
    if (!g_mute) playMelody(MEL_MUTE_OFF);
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
  int gpu, pcbatt, pcchg, diskR, diskW;
  char top[16];
  bool connected;
  uint32_t last;
  portENTER_CRITICAL(&g_mux);
  cpu = g_cpu; ram = g_ram; temp = g_temp; net = g_net; procs = g_procs;
  gpu = g_gpu; pcbatt = g_batt; pcchg = g_charging;
  diskR = g_diskR; diskW = g_diskW;
  strncpy(top, g_top, 15); top[15] = '\0';
  connected = g_connected; last = g_lastPacket;
  portEXIT_CRITICAL(&g_mux);

  // stale link -> treat as disconnected after 6 s
  if (connected && millis() - last > 6000) connected = false;

  // ---- alert beeps on mood escalation ----
  Mood mood = connected ? currentMood(cpu, ram, temp, gpu, pcbatt, pcchg) : M_SLEEP;
  if (connected && mood != g_prevMood &&
      (mood == M_PANIC || mood == M_HOT)) {
    playMelody(MEL_ALERT);
  }
  g_prevMood = mood;

  // ---- 1 Hz tick ----
  if (millis() - g_lastTick1s >= 1000) {
    g_lastTick1s = millis();
    g_uptimeSec++;
    if (mood == M_PANIC) {
      g_panicSec++;
      int tier = panicTier(g_panicSec);
      int prevTier = panicTier(g_panicSec - 1);
      if (tier != prevTier) {
        if (tier == 2) playMelody(MEL_PANIC2);
        if (tier == 3) playMelody(MEL_PANIC3);
      }
    } else {
      g_panicSec = 0;
    }
  }

  // ---- ENV III read (PCP-005): slow, gated; I2C reads block ----
  if (g_envPresent && millis() - g_lastEnvRead >= 2000) {
    g_lastEnvRead = millis();
    if (g_sht30.update())   { g_envTemp = g_sht30.cTemp; g_envHum = g_sht30.humidity; }
    if (g_qmp6988.update()) { g_envPress = g_qmp6988.pressure / 100.0f; }  // Pa -> hPa
  }

  // ---- pressure trend log (PCP-007): ~1/min ring buffer ----
  if (g_envPresent && g_envPress > 0 && millis() - g_lastPressLog >= PRESS_LOG_MS) {
    g_lastPressLog = millis();
    g_pressHist[g_pressHistPos] = g_envPress;
    g_pressHistPos = (g_pressHistPos + 1) % PRESS_HIST;
    if (g_pressHistN < PRESS_HIST) g_pressHistN++;
  }

  // ---- ENV telemetry to PC (PCP-009): device -> PC over TX notify ----
  if (g_envPresent && connected && g_txChar &&
      millis() - g_lastEnvSend >= ENV_SEND_MS) {
    g_lastEnvSend = millis();
    char line[48];
    snprintf(line, sizeof(line), "ENV;temp=%.1f;hum=%d;press=%d",
             g_envTemp, (int)(g_envHum + 0.5f), (int)(g_envPress + 0.5f));
    g_txChar->setValue((uint8_t*)line, strlen(line));
    g_txChar->notify();
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
      playMelody(MEL_LOWBATT);
    }
  }
  g_battWasLow = battLow;

  // ---- render selected view (skipped while the screen is off) ----
  if (screenOn) {
    switch (g_view) {
      case VIEW_PET:   viewPet(cpu, ram, temp, net, procs, top, gpu, pcbatt, pcchg, connected, g_frame); break;
      case VIEW_STATS: viewStats(cpu, ram, temp, net, procs, top, gpu, pcbatt, pcchg, connected, diskR, diskW); break;
      case VIEW_GRAPH: viewGraph(cpu, connected);                                     break;
      case VIEW_PROCS: viewProcs(cpu, ram, temp, net, procs, top, connected);         break;
      case VIEW_ENV:   viewEnv(cpu, ram, temp, net, procs, top, connected);           break;
    }
    canvas.pushSprite(0, 0);
  }

  g_frame++;
  delay(screenOn ? 55 : 150);   // ~18 fps awake, slower when display is off
}
