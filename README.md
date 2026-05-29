# PC-Pet — a Tamagotchi for the M5StickC Plus2

A virtual creature on the M5StickC Plus2 whose mood mirrors the state of your
computer. The PC streams metrics (CPU, RAM, GPU, temperature, network, disk,
battery, top processes) to the device over BLE; the device renders an animated
pet plus several info screens.

## Layout

```
pc-pet/
├── README.md                     # this file
├── ROADMAP.md                    # planned features (next versions)
├── CLAUDE.md                     # working notes / conventions for Claude Code
├── firmware/
│   └── pc_tamagotchi/
│       └── pc_tamagotchi.ino     # device firmware (Arduino C++, M5Unified + BLE)
├── agent/
│   ├── pc_pet_agent.py           # host agent (Python, psutil + bleak)
│   ├── scan.py                   # BLE scan diagnostic
│   └── requirements.txt
├── tools/
│   └── cpu_stress.py             # CPU load generator for testing moods
└── docs/
    └── protocol.md               # BLE packet protocol reference
```

## Architecture

BLE over the Nordic UART Service. The **device is the peripheral** (GATT
server); the **PC is the central**, which writes metric packets into the RX
characteristic. See `docs/protocol.md` for the wire format.

- Device firmware: Arduino C++ with M5Unified (display/IMU/power/buzzer) and the
  bundled ESP32 BLE library.
- Host agent: Python with `psutil` (metrics) and `bleak` (BLE). On Apple Silicon,
  CPU temperature and GPU load come from `macmon` (sudoless); battery from psutil.

## Hardware

- M5StickC Plus2 (ESP32-PICO-V3-02), USB-serial chip CH9102 (needs the CH9102/CH34x
  VCP driver on macOS; the port shows up as `/dev/cu.wchusbserial*`).
- Optional: ENV III HAT (SHT30 + QMP6988) on the top connector for room
  temperature / humidity / pressure — see ROADMAP.

## Build & flash the firmware

1. Arduino IDE 2.x. Add the M5Stack board package via Boards Manager URL
   `https://static-cdn.m5stack.com/resource/arduino/package_m5stack_index.json`.
2. Install libraries: M5Unified, M5GFX.
3. Board: `M5StickCPlus2`. Select the `wchusbserial` port. Close the Serial
   Monitor before uploading (it holds the port).
4. Upload. On success you will see `Hard resetting via RTS pin`.
5. Serial Monitor at 115200 shows boot, BLE connect, and `RX(...)` packet lines.

## Run the agent

```bash
cd agent
python3 -m pip install -r requirements.txt
# Apple Silicon: for CPU temp + GPU load
brew install vladkens/tap/macmon

python3 pc_pet_agent.py
# options: --name PCpet --interval 1.5 --address <BLE-addr>
```

`scan.py` lists all advertising BLE devices — useful to confirm the stick is
visible and to grab its address.

## Testing the moods

`tools/cpu_stress.py` generates synthetic CPU load so you can watch the pet move
through its moods without real work. Standard library only, no dependencies.

```bash
cd tools
python3 cpu_stress.py                 # ~100% on all cores until Ctrl+C
python3 cpu_stress.py --percent 70    # hold ~70% overall
python3 cpu_stress.py --ramp          # step 10 -> 40 -> 70 -> 95 and back
python3 cpu_stress.py --percent 90 --duration 30
```

Mood thresholds (from the firmware): CPU < 15 sleeps, >= 50 is busy, >= 85 panics.
`--ramp` walks those boundaries automatically, holding each level (`--hold`, 15 s
default) so you can see each state on the device.

## Controls

- **BtnA** (front, M5 logo): cycle screens — Pet -> Stats -> Graph -> Procs.
- **BtnB** (side): single click cycles the character; double click toggles mute.
- **Power button** (lower left): short click toggles the screen on/off; hold ~1 s
  powers the device off. A sustained strong shake also wakes the screen.

## Screens

- **Pet** — the creature + mood word + CPU/RAM bars + footer (temp / GPU / PC battery).
- **Stats** — CPU / RAM / GPU / TEMP bars + battery / net / top process.
- **Graph** — scrolling CPU history.
- **Procs** — top processes by CPU and, separately, by RAM.

## Characters

Blobby, Cat, Robo, Ghost, Bunny — cycled with a single click on BtnB. All share
the same mood-driven expressions; only the body silhouette differs.

## Known notes

- macOS may cache the GATT table after repeated reflashes; if writes stop
  arriving, toggle Bluetooth off/on (or reset the Bluetooth module), then restart
  the agent.
- Battery percentage on the stick is voltage-derived and approximate.
