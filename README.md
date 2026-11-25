# Ronin C6

Frame a camera remotely: drive a DJI Ronin gimbal from a web browser or a PS5 controller.

I built it to control a camera from 150 m away during my university's graduation ceremony,
with the equipment I already had.

## How it works

Browser → local Wi-Fi → ESP32-C6 → Bluetooth LE → gimbal

The ESP32 serves the control page itself and translates commands into DJI's DUML protocol.
No PC service is required.

- Pan/tilt with a virtual joystick or a game controller
- Zoom through the Focus Pro Motor (RS 3, RS 4 Pro)
- Five presets stored on the ESP32
- macOS app to flash the firmware and set up Wi-Fi over USB

## Status

Prototype, used live. Movement first observed on RS 3. RS 4 Pro is not fully validated,
RS 4 and RS 5 are not validated.

## Getting started

1. Download the macOS app (13+) from the [Releases](https://github.com/tlacassi/ronin-c6/releases/latest).
2. Plug in the ESP32-C6 over USB, install the firmware, enter your Wi-Fi.
3. Open `http://ronin-c6.local/`.

Firmware written in C with ESP-IDF 5.2 and FreeRTOS.

> No authentication or encryption: trusted, isolated networks only.
> A software stop is not an emergency stop.

## Credits

Protocol based on [DJI-Ronin-Bluetooth-Connector](https://github.com/aronesztergomi/DJI-Ronin-Bluetooth-Connector) (MIT).
See `NOTICE` and `LICENSE.upstream`. Not affiliated with DJI.

[Detailed notes (in French)](docs/README.fr.md)
