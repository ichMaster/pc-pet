// pet_ble.ino -- BLE receive path: callbacks + packet parsing.
//
// Arduino tab (concatenated onto the main sketch). Globals live in pet_state.h,
// constants in pet_config.h. The onWrite callback runs on the small-stack BT
// task, so it only stashes raw bytes; parsePacket() runs later on the main task.

// Copy raw RX bytes into g_rxBuf and flag for the main loop (BT task context).
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

// Parse "cpu,ram,temp,net,procs,topname,gpu,batt,charging,diskR,diskW;cpuList;
// ramList" and update shared state. Runs on the main task.
void parsePacket(char* buf) {
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
    BLEDevice::startAdvertising();
  }
};
