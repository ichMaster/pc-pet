// pet_types.h -- shared types for the PC-Pet sketch.
//
// Included at the top of pc_tamagotchi.ino so these types are visible both to
// the Arduino-generated function prototypes and to every concatenated .ino
// tab (pet_helpers.ino, pet_render.ino).
#pragma once
#include <Arduino.h>

// Melody note for the buzzer / speaker (PCP-002).
struct MelNote { uint16_t freq; uint16_t durMs; uint16_t pauseMs; };

// Pet moods. M_-prefixed: the ESP32 ROM headers already define BUSY/HOT.
enum Mood { M_SLEEP, M_HAPPY, M_BUSY, M_STUFFED, M_HOT, M_PANIC, M_LOWPWR };

// Cyclable screens (BtnA).
enum View { VIEW_PET, VIEW_STATS, VIEW_GRAPH, VIEW_PROCS, VIEW_ENV, VIEW_COUNT };

// ENV mood-modifier states (PCP-008).
enum EnvMod { ENV_NONE, ENV_STUFFY, ENV_WEATHER };

// One top-process entry (name + percentage).
struct ProcEntry { char name[14]; int val; };
