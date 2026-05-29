#!/usr/bin/env python3
"""
PC-Pet host agent
-----------------
Reads system metrics with psutil and streams them over BLE to the
M5StickC Plus2 running pc_tamagotchi.ino.

Packet (ASCII): cpu,ram,temp,net,procs,topname,gpu,batt,charging;cpuList;ramList
  cpuList / ramList = "name:val,name:val" (top processes)
  temp/gpu/batt = -1 when unavailable; charging = 0/1
On macOS (Apple Silicon) temp + GPU load come from `macmon` (brew install
vladkens/tap/macmon); battery comes from psutil.

Install:
    pip install bleak psutil
Run:
    python pc_pet_agent.py
    python pc_pet_agent.py --name PCpet --interval 1.5
"""

import argparse
import asyncio
import json
import shutil
import subprocess
import sys
import threading
import time

import psutil
from bleak import BleakClient, BleakScanner

# Nordic UART Service - must match the firmware
RX_UUID = "6e400002-b5a3-f393-e0a9-e50e24dcca9e"   # we write here
DEFAULT_NAME = "PCpet"


def _parse_macmon(out: str):
    """Parse macmon's JSON output (single object, pretty or compact)."""
    out = (out or "").strip()
    if not out:
        return None
    try:
        return json.loads(out)                       # whole output = one object
    except Exception:
        pass
    for line in reversed(out.splitlines()):          # one compact object per line
        line = line.strip()
        if line.startswith("{"):
            try:
                return json.loads(line)
            except Exception:
                continue
    for chunk in reversed(out.replace("}\n{", "}\x00{").split("\x00")):
        try:
            return json.loads(chunk)
        except Exception:
            continue
    return None


class MacMon:
    """Background reader for CPU temperature + GPU load on Apple Silicon.
    Runs `macmon` (sudoless) every few seconds in a thread so the async
    BLE loop never blocks. Values are -1 when unavailable."""

    def __init__(self, period: float = 3.0):
        self.path = shutil.which("macmon")
        self.temp = -1
        self.gpu = -1
        self._period = period
        self._lock = threading.Lock()
        if self.path:
            threading.Thread(target=self._loop, daemon=True).start()

    def _sample(self):
        try:
            out = subprocess.run([self.path, "raw", "-s", "1"],
                                 capture_output=True, text=True, timeout=8).stdout
        except Exception:
            return None, None
        d = _parse_macmon(out)
        if not d:
            return None, None
        t = d.get("temp", {}).get("cpu_temp_avg")
        gu = d.get("gpu_usage")
        g = gu[1] * 100 if isinstance(gu, list) and len(gu) >= 2 else None
        return (int(round(t)) if t else None,
                int(round(g)) if g is not None else None)

    def _loop(self):
        while True:
            t, g = self._sample()
            with self._lock:
                if t is not None:
                    self.temp = t
                if g is not None:
                    self.gpu = g
            time.sleep(self._period)

    def read(self):
        with self._lock:
            return self.temp, self.gpu


_macmon = MacMon()


def read_temp_gpu() -> tuple[int, int]:
    """Return (cpu_temp_C, gpu_load_pct); -1 for anything unavailable."""
    if sys.platform == "darwin":
        return _macmon.read()
    # Linux fallback: psutil temps, no GPU
    temp = -1
    fn = getattr(psutil, "sensors_temperatures", None)
    if fn:
        try:
            temps = fn() or {}
            for key in ("coretemp", "k10temp", "cpu_thermal", "acpitz", "zenpower"):
                if temps.get(key):
                    temp = int(temps[key][0].current)
                    break
            else:
                for entries in temps.values():
                    if entries:
                        temp = int(entries[0].current)
                        break
        except Exception:
            pass
    return temp, -1


def read_battery() -> tuple[int, int]:
    """Return (percent, charging) for the host machine; (-1, 0) if none."""
    fn = getattr(psutil, "sensors_battery", None)
    if not fn:
        return -1, 0
    try:
        b = fn()
    except Exception:
        return -1, 0
    if b is None:
        return -1, 0
    return int(round(b.percent)), (1 if b.power_plugged else 0)


class Metrics:
    """Collects metrics; keeps net + per-process CPU state between calls."""

    def __init__(self):
        psutil.cpu_percent(None)              # prime overall CPU
        io = psutil.net_io_counters()
        self._net_bytes = io.bytes_sent + io.bytes_recv
        self._net_t = time.time()
        self._primed = False

    @staticmethod
    def _clean(name: str, width: int = 10) -> str:
        # strip our delimiters and keep it short for the 135px screen
        return (name or "?").replace(",", " ").replace(":", " ").replace(";", " ")[:width]

    def _top_lists(self, n: int = 4):
        """Return (busiest_name, cpu_list_str, ram_list_str).
        cpu_list/ram_list look like 'name:val,name:val' (val = percent)."""
        rows = []
        for p in psutil.process_iter(["name"]):
            try:
                c = p.cpu_percent(None)       # since last call, non-blocking
                m = p.memory_percent()        # % of physical RAM
            except (psutil.NoSuchProcess, psutil.AccessDenied):
                continue
            rows.append((p.info.get("name") or "?", c, m))

        top_cpu = sorted(rows, key=lambda r: r[1], reverse=True)[:n]
        top_ram = sorted(rows, key=lambda r: r[2], reverse=True)[:n]

        cpu_list = ",".join(f"{self._clean(nm)}:{int(round(c))}" for nm, c, _ in top_cpu)
        ram_list = ",".join(f"{self._clean(nm)}:{int(round(m))}" for nm, _, m in top_ram)
        busiest = self._clean(top_cpu[0][0], 12) if top_cpu else "-"
        return busiest, cpu_list, ram_list

    def sample(self) -> str:
        cpu = int(round(psutil.cpu_percent(None)))
        ram = int(round(psutil.virtual_memory().percent))
        temp, gpu = read_temp_gpu()
        batt, charging = read_battery()

        now = time.time()
        io = psutil.net_io_counters()
        total = io.bytes_sent + io.bytes_recv
        dt = max(now - self._net_t, 1e-3)
        net_kbs = int((total - self._net_bytes) / dt / 1024)
        net_kbs = max(0, min(net_kbs, 9999))
        self._net_bytes, self._net_t = total, now

        procs = len(psutil.pids())
        busiest, cpu_list, ram_list = self._top_lists(4)

        # first sample after priming is noisy; that's fine
        self._primed = True
        return (f"{cpu},{ram},{temp},{net_kbs},{procs},{busiest},"
                f"{gpu},{batt},{charging};{cpu_list};{ram_list}")


SERVICE_UUID = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"


async def find_device(name: str, address: str | None = None):
    """Find the stick by name substring OR by our NUS service UUID.
    If --address was given, match that exact address instead."""
    print(f"[scan] looking for '{name}' (or service {SERVICE_UUID[:8]}...) ...")
    found = {}

    def cb(device, adv):
        found[device.address] = (device, adv)

    scanner = BleakScanner(detection_callback=cb)
    await scanner.start()
    await asyncio.sleep(6.0)
    await scanner.stop()

    for addr, (dev, adv) in found.items():
        if address and addr.lower() == address.lower():
            print(f"[scan] matched address {addr}")
            return dev
        nm = (adv.local_name or dev.name or "").lower()
        uuids = [u.lower() for u in (adv.service_uuids or [])]
        if (not address) and (name.lower() in nm or SERVICE_UUID in uuids):
            print(f"[scan] found '{adv.local_name or dev.name}' at {addr}")
            return dev
    return None


async def stream(dev, metrics: Metrics, interval: float):
    async with BleakClient(dev) as client:
        print(f"[ble ] connected to {dev.address}")
        # warm up per-process counters so the first 'top' is meaningful
        metrics._top_lists()
        await asyncio.sleep(min(interval, 1.0))
        while client.is_connected:
            packet = metrics.sample()
            try:
                await client.write_gatt_char(RX_UUID, packet.encode(), response=True)
                print(f"[send] {packet}")
            except Exception as e:
                print(f"[ble ] write failed: {e}")
                break
            await asyncio.sleep(interval)
    print("[ble ] disconnected")


async def main():
    ap = argparse.ArgumentParser(description="Stream PC metrics to M5StickC Plus2")
    ap.add_argument("--name", default=DEFAULT_NAME, help="BLE device name")
    ap.add_argument("--interval", type=float, default=1.5,
                    help="seconds between updates")
    ap.add_argument("--address", default=None,
                    help="connect to this exact BLE address (from scan.py)")
    args = ap.parse_args()

    metrics = Metrics()
    print("PC-Pet agent - Ctrl+C to quit")
    while True:
        try:
            dev = await find_device(args.name, args.address)
            if not dev:
                print("[scan] not found, retrying in 5s")
                await asyncio.sleep(5)
                continue
            await stream(dev, metrics, args.interval)
        except Exception as e:
            print(f"[err ] {e}")
        print("[main] reconnecting in 3s ...")
        await asyncio.sleep(3)


if __name__ == "__main__":
    try:
        asyncio.run(main())
    except KeyboardInterrupt:
        print("\nbye")
        sys.exit(0)
