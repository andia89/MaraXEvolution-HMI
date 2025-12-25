# MaraX Evolution - HMI

This repository contains the Nextion display files and firmware for the **MaraX Evolution** Human-Machine Interface. It provides a visual interface for monitoring temperature, pressure, and shot timers.

## Repository Structure

* `HMI/`: Nextion Editor source file (`.hmi`), compiled file (`.tft`), and Python scripts for asset generation.
* `firmware/`: PlatformIO project for the HMI controller (ESP32/ESP8266 based).

## Assembly & Installation

### Display Setup
1.  Copy the `HMI/nextion.tft` file to the root of a FAT32-formatted microSD card.
2.  Insert the card into the Nextion display while powered off.
3.  Power on the display to flash the interface. Remove the card once complete.

### Controller Assembly
1.  Flash the firmware located in `firmware/` to your HMI microcontroller (see below).
2.  Connect the microcontroller to the Nextion display via UART (TX/RX).
3.  Connect the HMI unit to the main MaraX Evolution controller.

## Firmware Compilation

1.  Install [VSCode](https://code.visualstudio.com/) with the [PlatformIO extension](https://platformio.org/).
2.  Open the `firmware` directory in PlatformIO.
3.  Build and upload the code to your HMI microcontroller.

> **Note:** The Python scripts in `HMI/` (`createPNG.py`, etc.) are used to generate graphical assets for the display. You only need these if you plan to modify the UI graphics.

## Support

This is a supporting repository. For all inquiries, issues, or support requests, please visit the **[Main MaraX Evolution Repository](https://github.com/andia89/maraxevolution)**.

## Licensing
This project is dual-licensed to protect the work while allowing for personal study and modification.

Firmware: The source code located in the /firmware directory is licensed under the PolyForm Noncommercial License 1.0.0. You may modify and use it for personal projects, but you cannot use it for commercial products.

Hardware: The hardware designs, schematics, and 3D models located in the /HMI directory are licensed under the Creative Commons Attribution-NonCommercial 4.0 International (CC BY-NC 4.0) license.

Please see the LICENSE file in each respective subdirectory for the full legal text.
