/*
 *  PC-Pet  --  Tamagotchi for M5StickC Plus2
 *  -------------------------------------------------------------
 *  A virtual creature whose mood mirrors your computer's state.
 *  Metrics arrive over BLE (Nordic UART Service) from a Python
 *  host agent on the PC (see pc_pet_agent.py).
 *
 *  This is the main sketch tab: includes, setup(), and loop().
 *  Everything else is split into components (Arduino tabs compiled
 *  as one translation unit):
 *    pet_types.h    enums / structs
 *    pet_config.h   all constants + melody data
 *    pet_state.h    all mutable globals
 *    pet_ble.h      RX callbacks + packet parsing (header: classes used by setup)
 *    pet_audio.ino  melodies, siren, ambient, voice, heartbeat, chirps
 *    pet_mood.ino   mood / character / pressure / ENV-modifier logic
 *    pet_render.ino character art, pet renderer, view screens
 *
 *  Board:  M5StickC Plus2 (ESP32-PICO-V3-02)
 *  Libs :  M5Unified, M5Unit-ENV, ESP32 BLE (bundled with the core)
 * -------------------------------------------------------------- */

#include <M5Unified.h>
#include <BLEDevice.h>
#include <BLEServer.h>
#include <BLEUtils.h>
#include <BLE2902.h>
#include <math.h>
#include "M5UnitENV.h"     // ENV III HAT (SHT30 + QMP6988)
#include "pet_types.h"     // shared enums/structs (visible to auto-prototypes)
#include "pet_config.h"    // constants + melody data
#include "pet_assets.h"    // generated event-voice PCM clips (PCP-015)
#include "pet_state.h"     // mutable globals (must come after the libs above)
#include "pet_ble.h"       // BLE callback classes -- needed by setup() below

// =================  setup  ====================================
void setup() {
  auto cfg = M5.config();
  M5.begin(cfg);
  Serial.begin(115200);
  delay(200);
  Serial.println("\nPC-Pet booting...");
  M5.Display.setRotation(0);          // portrait 135 x 240
  M5.Display.setBrightness(FULL_BRI);

  // ---- audio routing (PCP-012) ----
  // For SPK2, point M5.Speaker at the HAT's I2S pins BEFORE begin(); otherwise
  // the built-in speaker config is left untouched. NOTE: M5Unified's
  // speaker_config_t field names vary by version -- verify on-device.
  if ((int)HAT_SELECT == HAT_SPK2) {
    auto spc = M5.Speaker.config();
    spc.pin_data_out = SPK2_PIN_DATA;
    spc.pin_bck      = SPK2_PIN_BCK;
    spc.pin_ws       = SPK2_PIN_WS;
    M5.Speaker.config(spc);
    g_hat = HAT_SPK2;
  }
  M5.Speaker.begin();
  M5.Speaker.setVolume(120);
  g_lastActivity = millis();

  for (int i = 0; i < HIST; i++) g_hist[i] = 0;

  if (!canvas.createSprite(M5.Display.width(), M5.Display.height())) {
    canvas.setColorDepth(8);          // fall back to 8-bit if 16-bit won't fit
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
  // On the main Wire bus (G0=SDA / G26=SCL). Skip the probe in SPK2 mode -- there
  // G0/G26 are the amp's I2S pins. begin() returns true even when absent, so
  // confirm with a real read.
  if (g_hat != HAT_SPK2) {
    g_sht30.begin(&Wire, SHT3X_I2C_ADDR, ENV_PIN_SDA, ENV_PIN_SCL, ENV_I2C_HZ);
    g_qmp6988.begin(&Wire, QMP6988_SLAVE_ADDRESS_L, ENV_PIN_SDA, ENV_PIN_SCL, ENV_I2C_HZ);
    g_envPresent = g_sht30.update() && g_qmp6988.update();
    if (g_envPresent) g_hat = HAT_ENV;
  }
  Serial.printf("HAT: %s\n", g_hat == HAT_SPK2 ? "SPK2"
                           : g_hat == HAT_ENV  ? "ENV III" : "none");

  playVoice(MEL_BOOT, VOICE_BOOT_PCM, VOICE_BOOT_LEN);   // boot chime (PCP-015, SPK2 only)

  // ---- BLE peripheral ----
  BLEDevice::init(DEVICE_NAME);
  BLEDevice::setMTU(BLE_MTU);
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
  g_txChar = tx;   // PCP-009: handle for ENV notify from loop()

  svc->start();

  BLEAdvertising* adv = BLEDevice::getAdvertising();
  // Name in the primary packet (reliable name discovery on macOS); 128-bit
  // service UUID in the scan response so the 31-byte main packet doesn't overflow.
  BLEAdvertisementData advData;
  advData.setFlags(0x06);
  advData.setName(DEVICE_NAME);
  adv->setAdvertisementData(advData);
  BLEAdvertisementData scanResp;
  scanResp.setCompleteServices(BLEUUID(SERVICE_UUID));
  adv->setScanResponseData(scanResp);
  adv->setScanResponse(true);
  BLEDevice::startAdvertising();
}

// =================  main loop  ================================
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
          if (g_shakeStart == 0 || now - g_lastShake > 400) g_shakeStart = now;
          g_lastShake = now;
          if (now - g_shakeStart >= SHAKE_HOLD_MS) {   // shaken long enough -> wake
            bool wasOff = g_forceOff || (now - g_lastActivity >= OFF_AFTER_MS);
            g_lastActivity = now;
            g_forceOff = false;
            g_shakeStart = 0;
            if (wasOff) playMelody(MEL_WAKE);   // PCP-013 wake cue
          }
        } else if (now - g_lastShake > 400) {
          g_shakeStart = 0;
        }
      }
      g_lax = ax; g_lay = ay; g_laz = az;
      g_accelInit = true;
    }
  }

  // ---- drain any received BLE packet (parsed on the main task) ----
  if (g_rxReady) {
    char local[RX_BUF_SIZE];
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

  // ---- 1 Hz debug heartbeat ----
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
  if (M5.BtnPWR.wasClicked()) {
    g_forceOff = !g_forceOff;
    if (!g_forceOff) g_lastActivity = millis();
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

  if (connected && millis() - last > STALE_LINK_MS) connected = false;

  // ---- alert beeps + event voice on mood escalation (PCP-015) ----
  Mood mood = connected ? currentMood(cpu, ram, temp, gpu, pcbatt, pcchg) : M_SLEEP;
  if (connected && mood != g_prevMood) {
    if (mood == M_PANIC)       playMelody(MEL_ALERT);
    else if (mood == M_HOT)    playVoice(MEL_OVERHEAT, VOICE_OVERHEAT_PCM, VOICE_OVERHEAT_LEN);
    else if (mood == M_LOWPWR) playVoice(MEL_LOWVOICE, VOICE_LOWPWR_PCM, VOICE_LOWPWR_LEN);
  }
  g_prevMood = mood;

  if (connected && !g_prevConnected) playVoice(MEL_ONLINE, VOICE_ONLINE_PCM, VOICE_ONLINE_LEN);   // back online
  g_prevConnected = connected;

  // ---- 1 Hz tick ----
  if (millis() - g_lastTick1s >= 1000) {
    g_lastTick1s = millis();
    g_uptimeSec++;
    if (mood == M_PANIC) g_panicSec++;   // tier audio handled by the siren
    else                 g_panicSec = 0;
  }

  // ---- non-blocking audio engines (PCP-013/014/016) ----
  {
    int ptier = (mood == M_PANIC) ? panicTier(g_panicSec) : 0;
    tickSiren(ptier);
    tickAmbient(mood, ptier);
    tickHeartbeat(cpu);
    tickChirp(cpu, ram, gpu, temp);
  }

  // ---- ENV III read (PCP-005): slow, gated; I2C reads block ----
  if (g_envPresent && millis() - g_lastEnvRead >= ENV_READ_MS) {
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
    bool justCrossed = !g_battWasLow;
    bool dueAgain = (millis() - g_lastBattBeep > LOW_BATT_REPEAT);
    if (justCrossed || dueAgain) {
      g_lastBattBeep = millis();
      g_lastActivity = millis();
      playMelody(MEL_LOWBATT);
    }
  }
  g_battWasLow = battLow;

  // ---- render the selected view (skipped while the screen is off) ----
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
  delay(screenOn ? FRAME_MS_ON : FRAME_MS_OFF);
}
