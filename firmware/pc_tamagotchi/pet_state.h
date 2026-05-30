// pet_state.h -- all mutable global state for PC-Pet.
//
// Defined in a header (not a tab) and #included near the top of the main sketch
// so every global exists BEFORE setup()/loop() and the function tabs reference
// it. Single translation unit + #pragma once => these definitions are emitted
// exactly once. Constants and melody data live in pet_config.h; types in
// pet_types.h.
#pragma once
#include <M5Unified.h>
#include <BLEDevice.h>
#include "M5UnitENV.h"
#include "pet_types.h"
#include "pet_config.h"

// ---- shared metrics (BLE task writes via parse on main task; g_mux-guarded) --
portMUX_TYPE g_mux = portMUX_INITIALIZER_UNLOCKED;
volatile int  g_cpu = 0, g_ram = 0, g_temp = -1, g_net = 0, g_procs = 0;
volatile int  g_gpu = -1, g_batt = -1, g_charging = 0;   // GPU %, PC batt %, charging
volatile int  g_diskR = 0, g_diskW = 0;
char          g_top[16] = "-";
volatile bool g_connected = false;
volatile uint32_t g_lastPacket = 0;
BLECharacteristic* g_txChar = nullptr;   // device -> PC notify (PCP-009)

// ---- top processes (main task only) ----
ProcEntry g_cpuProcs[NPROC];
int       g_cpuProcN = 0;
ProcEntry g_ramProcs[NPROC];
int       g_ramProcN = 0;

// ---- CPU history ring (graph) ----
uint8_t g_hist[HIST];
int     g_histPos = 0;

// ---- UI state ----
int  g_view = VIEW_PET;
bool g_mute = false;
int  g_char = 0;

// ---- screen power management ----
uint32_t g_lastActivity = 0;
int      g_curBri = -1;
float    g_lax = 0, g_lay = 0, g_laz = 0;
bool     g_accelInit = false;
bool     g_forceOff  = false;   // power-button short press forced the screen off
uint32_t g_shakeStart = 0;      // when the current shake burst began (0 = none)
uint32_t g_lastShake  = 0;      // last strong-shake sample time

// ---- low-battery alert ----
bool     g_battWasLow  = false;
uint32_t g_lastBattBeep = 0;

// ---- 1 Hz tick (PCP-001) ----
uint32_t g_lastTick1s = 0;
uint32_t g_uptimeSec  = 0;
uint32_t g_panicSec   = 0;

// ---- ENV III HAT (PCP-005): SHT30 + QMP6988 on Wire(0,26) ----
SHT3X    g_sht30;
QMP6988  g_qmp6988;
bool     g_envPresent = false;
float    g_envTemp  = 0;     // room temperature, Celsius
float    g_envHum   = 0;     // relative humidity, %
float    g_envPress = 0;     // barometric pressure, hPa
uint32_t g_lastEnvRead = 0;

// ---- HAT mode resolved at boot (PCP-012) ----
HatMode  g_hat = HAT_NONE;

// ---- pressure trend ring (PCP-007) ----
float    g_pressHist[PRESS_HIST];
int      g_pressHistN = 0;
int      g_pressHistPos = 0;
uint32_t g_lastPressLog = 0;

// ---- ENV telemetry (PCP-009) ----
uint32_t g_lastEnvSend = 0;

// ---- offscreen canvas ----
M5Canvas canvas(&M5.Display);

// ---- BLE RX hand-off (callback -> loop) ----
volatile bool   g_rxReady = false;
uint8_t         g_rxBuf[RX_BUF_SIZE];
volatile size_t g_rxLen   = 0;
volatile uint32_t g_writeCount  = 0;   // debug counters
volatile size_t   g_dbgGetLen   = 0;
volatile size_t   g_dbgParamLen = 0;

// ---- animation + edge-trigger state ----
uint32_t g_frame = 0;
Mood     g_prevMood = M_HAPPY;
bool     g_prevConnected = false;   // "back online" event (PCP-015)

// ---- non-blocking panic siren state (PCP-013) ----
int      g_sirenTier = 0;
int      g_sirenIdx  = 0;
uint32_t g_sirenNext = 0;

// ---- mood ambient state (PCP-014) ----
int      g_ambMood = -1;
int      g_ambIdx  = 0;
uint32_t g_ambNext = 0;
int      g_ambVol  = 0;

// ---- heartbeat + reactive chirp state (PCP-016) ----
bool     g_heartbeatOn = false;     // off by default
uint32_t g_lastBeat  = 0;
uint32_t g_lastChirp = 0;
int      g_pchCpu = 0, g_pchRam = 0, g_pchGpu = 0, g_pchTemp = 0;
