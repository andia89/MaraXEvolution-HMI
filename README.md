# MaraX Evolution - HMI

This repository contains the hardware designs, 3D printed parts, and firmware for the **MaraX Evolution Human-Machine Interface (HMI)**.

Designed to replace the standard LEDs and switches of the Lelit Mara X, this unit provides a rich touchscreen interface, real-time extraction charting, and a physical rotary encoder for tactile control. It communicates wirelessly with the [Main Controller](https://github.com/andia89/maraxevolution) via ESP-NOW, ensuring a low-latency, wire-free installation.

## Features

### Visual Interface

* **Real-Time Charting:** Live visualization of **Pressure (bar)** and **Flow Rate (g/s)** during the shot.

* **Live Telemetry:** Monitors Boiler Temp, HX Temp, Shot Timer, and Weight.

* **System Status:** Clear indicators for Heating, Ready, Steam Boost, and Error states.

### Control & Profiling

* **Profile Editor:** Built-in **Web Server** to create, edit, and save complex pressure/flow profiles graphically from your phone or PC.

* **Profile Management:** Store up to **32 custom profiles** on the device.

* **Manual Control:** "Fly-by-wire" manual mode using the rotary encoder to control pump pressure/flow in real-time.

* **Configuration:** Adjust PID targets, brew temperatures, and steam settings directly from the screen.

### Maintenance & Tools

* **Scale Calibration:** On-screen wizard to tare and calibrate the [MaraX Evolution Scale](https://github.com/andia89/maraxevolution-scale).

* **Cleaning Assistant:** Guided backflush/cleaning cycles with on-screen instructions.

* **Auto-Pairing:** Seamlessly pairs with the Main Controller via ESP-NOW protocol.

## Repository Structure

* `HMI/`: Nextion Editor source files (`.hmi`), compiled binaries (`.tft`), and asset generation scripts.

* `firmware/`: PlatformIO project for the HMI controller (ESP32).

* `hardware/`: STL files for the 3D printed housing and mounting brackets.

## Hardware Overview

The HMI consists of:

1. **Microcontroller:** Arduino Nano ESP32 (handling Logic, WiFi, Web Server).

2. **Display:** Nextion Edge Series (NX4848E028-011C).

3. **Input:** Rotary Encoder and Push Button (for precise adjustments).

4. **Housing:** Custom 3D printed enclosure.

## [User Guide](USERGUIDE.md)

## [Parts list](PARTS.md)

## [Assembly Instructions](ASSEMBLY.md)

## Firmware Installation

The firmware is built using **PlatformIO**.

1. Install [VSCode](https://code.visualstudio.com/) and the [PlatformIO extension](https://platformio.org/).
2. Open the `firmware` directory in PlatformIO.

### 1. Initial Flash (USB)
For the first installation, you must connect the ESP32 directly to your computer.
1. Connect the ESP32 via USB.
2. Select the default environment (`env:hmi`) in PlatformIO.
3. Click the **Upload** button (Right Arrow icon).

### 2. Subsequent Updates (OTA)
Once the HMI is installed in the machine and connected to your WiFi, you can update it wirelessly.
1. Ensure your computer and the HMI are on the same WiFi network.
2. Select the ota environment (`env:hmi-ota`) in PlatformIO.
3. Click **Upload**. PlatformIO will detect the network port and flash the device over the air.

## Firmware Configuration

The HMI firmware is designed to run on an **Arduino Nano ESP32**.

1. **WiFi Setup:** On first boot, the HMI will create a WiFi Access Point (AP) named `esp32-arduino-screen-Setup`. Connect to it to configure your home WiFi.

2. **Web Interface:** Once connected, access the Profile Editor at `http://esp32-arduino-screen.local` (or the device IP).

3. **Pairing:** The HMI automatically broadcasts an ESP-NOW pairing request. Ensure the Main Controller is powered on; they will find each other automatically.

## Licensing

This project is dual-licensed to protect the work while allowing for personal study and modification.

* **Firmware:** The source code located in the `/firmware` directory is licensed under the **PolyForm Noncommercial License 1.0.0**. You may modify and use it for personal projects, but **commercial use is prohibited**.

  * *Third-Party Libraries included:* Modified versions of [Nextion_X2](https://github.com/sstaub/NextionX2) (MIT).

* **Hardware:** The hardware designs, schematics, and 3D models located in the `/HMI` and `/hardware` directories are licensed under the **Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0)** license.

Please see the [LICENSE](LICENSE) file for the full legal text.

*Part of the [MaraX Evolution Ecosystem](https://github.com/andia89/maraxevolution).*