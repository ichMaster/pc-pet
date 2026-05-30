// pet_config.h -- all tuning constants + melody data for PC-Pet.
//
// The single place to tweak behavior: BLE identity, HAT selection, thresholds,
// timings, pins, sizes, and every sound. Included near the top of the main
// sketch (after pet_types.h, before pet_state.h) so these are visible to every
// tab before any function references them.
#pragma once
#include <Arduino.h>
#include "pet_types.h"

// ---- BLE: Nordic UART Service ----
#define SERVICE_UUID "6E400001-B5A3-F393-E0A9-E50E24DCCA9E"
#define CHAR_RX_UUID "6E400002-B5A3-F393-E0A9-E50E24DCCA9E"  // PC writes here
#define CHAR_TX_UUID "6E400003-B5A3-F393-E0A9-E50E24DCCA9E"  // device notify
#define DEVICE_NAME  "PCpet"
#define BLE_MTU       185
#define RX_BUF_SIZE   200
const uint32_t STALE_LINK_MS = 6000;   // no packet this long -> disconnected

// ---- HAT selection (PCP-012) ----
// SPK2 (MAX98357 I2S amp) and ENV III share the top HAT port, and SPK2 has no
// I2C address so it cannot be auto-detected. Pick the fitted HAT here and
// reflash when you swap:
//   HAT_SELECT = -1        auto-probe ENV III; no HAT if absent (never SPK2)
//   HAT_SELECT = HAT_ENV   same as auto (probe ENV III)
//   HAT_SELECT = HAT_SPK2  force the SPK2 I2S amp (skips the ENV probe)
#define HAT_SELECT (-1)

// SPK2 I2S pins (MAX98357 on the top HAT connector).
#define SPK2_PIN_DATA 25       // DOUT = G25
#define SPK2_PIN_BCK  26       // BCLK = G26
#define SPK2_PIN_WS   0        // LRC  = G0

// ENV III I2C pins (SHT30 0x44 + QMP6988 0x70 on the main Wire bus).
#define ENV_PIN_SDA   0        // G0
#define ENV_PIN_SCL   26       // G26
#define ENV_I2C_HZ    400000U

// ---- sizes ----
#define NPROC 4                // top processes tracked per metric
#define HIST  110              // CPU history ring length (graph)
const int PRESS_HIST = 60;     // pressure-trend ring length (~1/min samples)
const int CHAR_COUNT = 5;      // Blobby, Cat, Robo, Ghost, Bunny

// ---- screen power management ----
const uint8_t  FULL_BRI      = 110;
const uint8_t  DIM_BRI       = 55;
const uint32_t DIM_AFTER_MS  = 10000;   // 10 s -> dim
const uint32_t OFF_AFTER_MS  = 30000;   // 30 s -> backlight off
const float    SHAKE_THRESH  = 1.2f;    // accel delta to count as shaking
const uint32_t SHAKE_HOLD_MS = 750;     // must shake ~0.75 s to wake

// ---- low-battery alert ----
const int      LOW_BATT_PCT    = 10;
const uint32_t LOW_BATT_REPEAT = 120000;   // re-beep every 2 min while low

// ---- panic tiers (PCP-003) ----
const uint32_t PANIC_T1 = 10;   // seconds to reach tier 2
const uint32_t PANIC_T2 = 30;   // seconds to reach tier 3

// ---- ENV mood modifier + cadences (PCP-005/007/008/009) ----
const float    ENV_STUFFY_TEMP = 27.0f;   // room temp C
const float    ENV_STUFFY_HUM  = 60.0f;   // humidity %
const uint32_t ENV_READ_MS     = 2000;    // sensor read cadence
const uint32_t PRESS_LOG_MS    = 60000;   // pressure-trend log cadence
const uint32_t ENV_SEND_MS     = 5000;    // ENV telemetry notify cadence

// ---- audio: channels + reactive tuning ----
const uint8_t  AMB_CH         = 1;       // ambient channel (cues/siren use 0)
const uint8_t  AMB_VOL        = 32;      // ambient target channel volume (0-255)
const uint32_t CHIRP_COOLDOWN = 1500;    // ms between reactive chirps (PCP-016)

// ---- frame pacing ----
const uint32_t FRAME_MS_ON  = 55;        // ~18 fps awake
const uint32_t FRAME_MS_OFF = 150;       // slower when the display is off

// =================  melody data  ==============================
// Each melody is a {freq, durMs, pauseMs} array terminated by {0,0,0}.

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
const MelNote MEL_WAKE[]       = { {1400, 25, 0}, {1900, 35, 0}, {0,0,0} };

// Panic siren patterns, looped by tickSiren (PCP-013)
const MelNote SIREN_T1[] = { {2200, 90, 500}, {0,0,0} };
const MelNote SIREN_T2[] = { {2000, 90, 70}, {2500, 90, 260}, {0,0,0} };
const MelNote SIREN_T3[] = { {2600, 110, 0}, {2200, 110, 0}, {0,0,0} };

// Mood ambient loops (PCP-014)
const MelNote AMB_HAPPY[]   = { {220, 120, 700}, {233, 120, 1400}, {0,0,0} };   // soft purr
const MelNote AMB_SLEEP[]   = { {180, 300, 2600}, {0,0,0} };                    // slow breathing
const MelNote AMB_BUSY[]    = { {520, 40, 180}, {520, 40, 700}, {0,0,0} };      // blip-blip
const MelNote AMB_STUFFED[] = { {300, 280, 3200}, {0,0,0} };                    // content sigh
const MelNote AMB_LOWPWR[]  = { {260, 200, 1800}, {200, 240, 2600}, {0,0,0} };  // faint whimper

// Event "voice" jingles (PCP-015) -- synth placeholders for real WAV/PCM
const MelNote MEL_BOOT[]     = { {880, 80, 20}, {1320, 80, 20}, {1760, 140, 0}, {0,0,0} };
const MelNote MEL_OVERHEAT[] = { {1600, 120, 40}, {1400, 120, 40}, {1200, 200, 0}, {0,0,0} };
const MelNote MEL_LOWVOICE[] = { {700, 160, 60}, {500, 160, 60}, {350, 260, 0}, {0,0,0} };
const MelNote MEL_ONLINE[]   = { {1046, 70, 20}, {1318, 70, 20}, {1568, 70, 20}, {2093, 150, 0}, {0,0,0} };
