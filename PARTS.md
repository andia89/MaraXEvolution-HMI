# MaraX Evolution HMI - Parts List

This document lists all the components required to build the MaraX Evolution Human-Machine Interface (HMI).

## Electronics & Display

| Component | Description | Quantity | Notes |
| :--- | :--- | :---: | :--- |
| **Microcontroller** | Arduino Nano ESP32 | 1 | Must include headers. Installs into the bottom section of the housing. |
| **Display** | Nextion [NX4848E028-011C](https://nextion.tech/) | 1 | 2.8" Intelligent Series HMI Touchscreen. |
| **Rotary Encoder** | Bourns PEC16-4125F-N0012 | 1 | Used for manual pressure control and menu navigation. |
| **Tactile Switch** | 506-FSM5JH | 1 | External push button for the encoder assembly (if not integrated). |
| **Cabling** | Dupont Cables (F-F / M-F) | Set | For connecting the Arduino to the Display and Encoder. |
| **Storage** | Micro SD Card | 1 | Required only for flashing the firmware onto the Nextion screen. |

## Housing & Hardware

| Component | Description | Quantity | Notes |
| :--- | :--- | :---: | :--- |
| **3D Printed Parts** | Custom Enclosure | 1 Set | STL files are located in `HMI/3DParts/`. I printed mine in PLA and was quite happy with it|
| **Threaded Inserts** | [M3 Threaded Inserts](https://www.ruthex.de/en/products/ruthex-gewindeeinsatz-m3-100-stuck-rx-m3x5-7-messing-gewindebuchsen) | 10 | Standard heat-set inserts (e.g., Ruthex M3x5.7). Any should do|
| **Teflon Plunger** | [M3 Teflon Plunger](https://de.aliexpress.com/item/1005009562484175.html) | 31 | **Critical:** Get the longest version available. Used to provide smooth rotation feel. |
| **Friction Tape** | [Teflon Tape (10mm)](https://de.aliexpress.com/item/1005005280321356.html) | 1 Roll | Applied between moving 3D printed parts to remove friction. |
| **Fasteners** | M3 Machine Screws | Assorted | A selection of lengths (6mm - 12mm) for assembling the case. |

---

*Note: Links provided are for reference and may change over time.*

