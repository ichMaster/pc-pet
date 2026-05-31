# PC-Pet FSM -- rule-based state machine (educational)

A teaching sketch showing one idea: a unified Tamagotchi **finite state machine**
whose transitions are configured by a **table of rules**, not scattered
`if/else`. Read alongside [`../firmware/pet_fsm/pet_fsm.ino`](../firmware/pet_fsm/pet_fsm.ino).

## The four parts of a classical FSM

| Part | In this sketch |
|------|----------------|
| **States** | `enum State { S_SLEEP, S_HAPPY, S_HUNGRY, S_PLAYING, S_PANIC }` -- exactly one is active at a time |
| **Events** | a 1 Hz tick, BtnA/BtnB presses, and the decaying `hunger`/`energy` stats |
| **Transitions** | the `RULES[]` table -- "from THIS state, IF condition, go to THAT" |
| **Actions** | `onEnter(state)` -- runs once when a state becomes active |

The golden rule: **rules only decide, actions only do.** Conditions are pure
(no side effects); all side effects (sounds, stat changes) live in `onEnter`.

## The machine is a table

The whole transition logic is data, not code:

```c
struct Rule {
  int    from;        // a State, or ANY_STATE (applies in any state)
  bool (*cond)();     // guard: true when this transition should fire
  State  to;          // destination
  const char* why;    // label, shown on screen
};

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
```

**Order = priority.** The engine checks rows top-to-bottom and the **first match
wins**, so "exhausted" and "starving" beat a button press, which beats the
calm/default rules. To change behaviour you edit the table -- add a row, reorder
for priority, swap a guard -- without touching the engine.

## The engine

The "state machine" is this tiny generic loop -- it knows nothing about pets:

```c
void step() {
  for (int i = 0; i < RULE_COUNT; i++) {
    const Rule& r = RULES[i];
    if (r.from != ANY_STATE && r.from != g_state) continue;  // wrong state
    if (r.to == g_state) continue;                           // no self-loop
    if (r.cond()) {                                          // guard passes?
      g_state = r.to;  g_lastWhy = r.why;  onEnter(g_state); // transition + act
      return;                                                // first match wins
    }
  }
}
```

## State diagram

```
                 cExhausted (energy<=15)  [from ANY]
        +-------------------------------------------------+
        v                                                 |
     [S_SLEEP] --cRested(energy>=90)--> [S_HAPPY] <--------+
        ^                                  |  ^
        |                       BtnA play  |  | BtnB fed / played-out (3s)
        |                                  v  |
   cExhausted                          [S_PLAYING]
   (from any)                              |
        |                                  | cPlayDone (>3s)
        |                                  v
        |   cStarving (hunger>=70) [from ANY]
        +------------------> [S_HUNGRY] --BtnB fed--> [S_HAPPY]
```

(S_PANIC exists in the enum as a slot to extend; no rule drives it yet -- a good
first exercise: add `{ ANY_STATE, cSomething, S_PANIC, "..." }`.)

## Why it's a *real* FSM (not a cascade)

The main firmware's `currentMood()` recomputes mood from inputs every loop and
ignores history -- simple, but it can't say "how I got here." This sketch keeps
**memory**:

- `g_state` persists between ticks; some rules are state-specific (`from: S_SLEEP`).
- `cPlayDone()` is a **timed transition** -- it fires 3 s after *entering*
  `S_PLAYING` (`millis() - g_inState > 3000`). Same inputs, different outcome
  depending on how long you've been in a state. That's the thing only a stateful
  machine can express.

## Seeing it work

The screen shows the current state **and the `why`** of the last transition, so
you can watch the rules fire:

- Idle: hunger rises ~3/s; at 70 it flips to **hungry** ("very hungry").
- Press **BtnB** (feed): hunger drops, state -> **happy** ("fed").
- Press **BtnA** (play): -> **playing**, energy drops; after 3 s -> **happy**
  ("played out").
- Energy falls while awake; at <=15 -> **sleep** ("no energy"), where it recovers,
  then -> **happy** at >=90 ("rested").

## How to extend (exercises)

1. **Add a state**: give `S_PANIC` a rule, e.g. fire when hunger AND low energy
   coincide.
2. **Add a guard**: a new `bool cXxx()` plus a `RULES[]` row.
3. **Change priority**: move a row up/down and watch behaviour shift.
4. **Per-state action**: extend `onEnter()` (a different sound, a visual).

## Relation to the full firmware

| pet_fsm (this) | pc_tamagotchi (full) |
|----------------|----------------------|
| `RULES[]` table + `step()` | `currentMood()` priority cascade |
| stateful, with timers/guards | mostly stateless recompute (+ panic-tier timer) |
| events: buttons, tick, stats | events: BLE metrics, ENV sensors |
| 1 screen / 1 face | 5 screens / 5 characters |

The rule-table approach is what you'd grow toward if mood logic became
configurable (e.g. transition thresholds pushed from the agent's `CFG;` channel).

## Build & run

Arduino IDE, board **M5StickC Plus2**, library **M5Unified** only. Open
`firmware/pet_fsm/pet_fsm.ino`, Upload. No agent/HAT needed.
