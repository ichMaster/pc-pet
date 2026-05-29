#!/usr/bin/env python3
"""
BLE scan diagnostic for PC-Pet.
Lists every advertising BLE device for ~10 seconds.

  python scan.py

If the list is EMPTY -> macOS hasn't granted Bluetooth permission to your
   terminal (System Settings > Privacy & Security > Bluetooth), or Bluetooth
   is off. Fix that first.
If you see other devices but NOT 'PCpet' -> the stick isn't advertising
   (firmware not running / device asleep). Re-check the upload and that the
   screen shows 'waiting for PC...'.
"""

import asyncio
from bleak import BleakScanner

NUS_SERVICE = "6e400001-b5a3-f393-e0a9-e50e24dcca9e"


async def main():
    print("Scanning 10s for BLE devices...\n")
    found = {}

    def cb(device, adv):
        found[device.address] = (device, adv)

    scanner = BleakScanner(detection_callback=cb)
    await scanner.start()
    await asyncio.sleep(10.0)
    await scanner.stop()

    if not found:
        print("NO devices seen at all.")
        print(">> Almost certainly a macOS Bluetooth permission issue.")
        print(">> System Settings > Privacy & Security > Bluetooth -> enable your terminal.")
        return

    print(f"Found {len(found)} device(s):\n")
    pcpet = None
    for addr, (dev, adv) in found.items():
        name = adv.local_name or dev.name or "(no name)"
        uuids = adv.service_uuids or []
        rssi = adv.rssi
        mark = ""
        if (name and "pcpet" in name.lower()) or (NUS_SERVICE in [u.lower() for u in uuids]):
            mark = "   <<< THIS LOOKS LIKE THE STICK"
            pcpet = (dev, adv)
        print(f"  {name:24s} {addr}  rssi={rssi}{mark}")
        if uuids:
            print(f"      services: {uuids}")

    print()
    if pcpet:
        dev, adv = pcpet
        print(f"PCpet is visible. address={dev.address}  advertised name={adv.local_name!r}")
        print("If the agent still can't find it by name, run the agent with --address (see note).")
    else:
        print("PCpet NOT in the list. The stick is not advertising the expected name/service.")


if __name__ == "__main__":
    asyncio.run(main())
