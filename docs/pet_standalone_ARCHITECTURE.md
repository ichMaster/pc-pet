# PC-Pet Standalone -- code & architecture

A teaching version of the PC-Pet firmware. One file, one screen, one
character. No BLE, no HAT sensors, no second task. The pet changes mood on a
random timer and reacts to two buttons. Read this alongside
[`pet_standalone.ino`](pet_standalone.ino).

## Why this exists

The full firmware (`firmware/pc_tamagotchi/`) is split across 8 files and juggles
two FreeRTOS tasks, a BLE link, I2C sensors, and a mutex. That is a lot to read
just to learn how an M5 sketch is shaped. This version strips all of that away so
the *skeleton* is visible: an Arduino program is `setup()` once, then `loop()`
forever, and a UI is "read input -> update state -> draw" repeated fast enough to
look alive.

## The control loop

Every interactive embedded program is the same shape. Here it is in `loop()`:

```
        +------------------------------------------+
        |              loop() (forever)            |
        |                                          |
   +--->|  1. read input    M5.update(), buttons   |
   |    |  2. update state  stats decay, mood roll |
   |    |  3. draw          render() -> canvas     |
   |    |  4. wait          ~60 ms (frame pacing)  |
   +----+------------------------------------------+
```

- **read input** -- `M5.update()` refreshes button state; we check
  `BtnA.wasClicked()` / `BtnB.wasClicked()`.
- **update state** -- time-driven changes: stats decay, and the mood "drifts" to
  a new random value when its timer expires.
- **draw** -- build a whole frame offscreen, then push it to the LCD in one shot.
- **wait** -- only redraw every `FRAME_MS`, so the loop doesn't burn 100% CPU.

Nothing blocks. We never `delay()` for seconds; instead we compare `millis()`
against a deadline. That keeps buttons responsive while the pet animates.

## File sections (the components)

The single `.ino` is organised into the same logical parts the big firmware
splits into separate files -- just kept together here for readability.

| Section | Role | Key symbols |
|---------|------|-------------|
| 1. CONFIG | tunable constants | `MOOD_MIN_MS`, `FRAME_MS`, `STAT_MAX`, `STAT_DECAY_MS` |
| 2. STATE | variables that change over time | `g_mood`, `g_happiness`, `g_fullness`, `g_frame`, `canvas` |
| 3. HELPERS | pure logic, no drawing | `moodWord()`, `bodyColor()`, `rollMood()` |
| 4. RENDER | draw to the offscreen canvas | `drawFace()`, `drawBar()`, `render()` |
| 5. `setup()` | one-time boot | display + speaker init, RNG seed, sprite alloc |
| 6. `loop()` | the control loop above | input -> state -> draw |

## State model

Five moods in one enum:

```
enum Mood { M_HAPPY, M_SAD, M_SLEEPY, M_HUNGRY, M_EXCITED, MOOD_COUNT };
```

`MOOD_COUNT` is a trick: because enum values count from 0, the last entry equals
"how many moods there are", so `random(MOOD_COUNT)` picks a valid mood. (The
`M_` prefix avoids clashing with macros the ESP32 ROM already defines.)

Two stats, 0..100, that **decay over time** so the buttons matter:

| Stat | Raised by | Falls by |
|------|-----------|----------|
| `g_happiness` ("joy") | BtnA "play" (+15) | 1 every `STAT_DECAY_MS` |
| `g_fullness` ("fed")  | BtnB "feed" (+20) | 1 every `STAT_DECAY_MS` |

## How the mood changes (the "autonomous" feel)

Two mechanisms, one overriding the other:

1. **Drift** -- every 6-12 s a timer (`g_nextMood`) fires and the pet picks a new
   mood via `rollMood()`. That function biases toward a *need*: if `fullness < 30`
   it goes hungry, if `happiness < 30` it goes sad, otherwise it's random. So the
   pet looks like it has a mind of its own, but its needs still show through.
2. **Reaction** -- a button press sets the mood immediately and holds it for
   `REACT_MS` (via `g_reactUntil`). The drift timer is skipped while a reaction is
   active, so your input always wins for a couple of seconds.

```
button press --> set mood + g_reactUntil = now + REACT_MS
                       |
   loop: if now >= g_reactUntil AND now >= g_nextMood:
                       |
                  g_mood = rollMood();  schedule next drift
```

## Rendering: offscreen canvas

All drawing goes into `canvas` (an `M5Canvas` = a sprite in RAM), then
`canvas.pushSprite(0,0)` copies the finished frame to the LCD at once. Drawing
shape-by-shape directly to the screen would flicker; compositing offscreen and
blitting once is smooth. `drawFace()` layers body -> eyes -> mouth -> per-mood
effect; animation comes from `g_frame` feeding `sinf()` (breathing) and modulo
counters (blink, sparkle).

## Controls

| Button | Action | Effect |
|--------|--------|--------|
| BtnA (front) | play | mood -> EXCITED, +joy, blip |
| BtnB (side) | feed | mood -> HAPPY, +fed, blip |
| Power (hold ~1 s) | power off | handled by M5Unified |

## How it maps to the full firmware

Same ideas, scaled up:

| Standalone | Full firmware (`pc_tamagotchi/`) |
|------------|----------------------------------|
| mood from a random timer | mood from PC metrics (`currentMood()`) |
| 1 character in `drawFace()` | 5 characters in `drawCharBody()` |
| 1 screen (`render()`) | 5 view screens + BtnA cycling |
| stats decay locally | metrics arrive over BLE from a Python agent |
| `M5.Speaker.tone()` inline | melody/siren/ambient engine (`pet_audio`) |
| single `loop()`, no locking | BLE task + main task, `g_mux` mutex |

Once this version makes sense, the full firmware is the same loop with real
inputs (BLE, sensors) feeding the same render step.

## Build & run

Arduino IDE, board **M5StickC Plus2**, library **M5Unified** only. Open
`pet_standalone.ino`, Upload. No agent, no HAT, nothing else required.
