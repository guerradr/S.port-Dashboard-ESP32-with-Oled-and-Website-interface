# ESP32 Telemetry Multitool (FrSky S.Port + Expandable Multi-Protocol HUD)

A direct-connect RC telemetry reader, 128×64 OLED HUD, and live engineering web dashboard built for the **ESP-WROOM-32 (`esp32dev`)** (and optionally **LOLIN C3 Pico**) with a **0.96" SSD1306 OLED**. Connects directly to a FrSky S.Port bus without external signal-inversion hardware, hosts a Wi-Fi Access Point (`SPort-HUD`) + WebSocket server, and drives a customizable local OLED display.

## Key Features
- **Zero-Inverter Direct S.Port:** Uses ESP32 hardware UART RX/TX inversion (`setRxInvert(true)` / `setTxInvert(true)`) at `57600` baud on `Serial2`.
- **Dual Customizable Displays:** Reconfigure OLED slots/presets and Web UI themes/settings live from the browser and persist them in non-volatile flash.
- **3 Use-Case Presets:**
  1. **Telemetry Info Only:** Pack voltage, per-cell voltage, current, speed, altitude, and RSSI overview.
  2. **EDF / Drivetrain Troubleshooting:** Real-time watts ($P = V \times I$), voltage sag, RPM, ESC/motor temperatures, peak values, and mAh consumption.
  3. **Logging & Sensor Troubleshooting:** Live S.Port packet rate (FPS), CRC error counter, last physical/application IDs, active sensor bitmask, and live telemetry/CSV logging in the web UI.
- **5 Switchable Web Themes:**
  - `A1`: Fighter Jet HUD
  - `A2`: Commercial Airliner PFD / ECAM
  - `A3`: GA Cockpit (Garmin G1000 inspired)
  - `B`: Modern Automotive Cluster
  - `C`: Marine Helm Display
- **Expandable Multi-Protocol Architecture:** Built on `TelemetryBus` and `ProtocolParser` with active `SportParser` (FrSky S.Port) and structured stubs in `src/FutureParsers.h` for **MSP (INAV/Betaflight)**, **MAVLink**, **CRSF**, and **Jeti EX**.

---

## Hardware Wiring (ESP-WROOM-32 / `esp32dev`)

> **3.3V Logic Warning:** ESP32 GPIOs are **3.3V** (not 5V tolerant). Power the dev board via `VIN` / `5V` from a regulated 5V BEC/RX rail and place a **1 kΩ – 2.2 kΩ series resistor** on the S.Port signal line to `GPIO 16` to limit fault/spike currents.

| ESP-WROOM-32 Pin | Connected To | Notes |
| :--- | :--- | :--- |
| **VIN / 5V** | Receiver Servo Rail `+` (5V) | Powers onboard 3.3V regulator |
| **GND** | Receiver `-` & OLED `GND` | Common ground |
| **3V3** | OLED `VCC` | 3.3V OLED supply |
| **GPIO 21** | OLED `SDA` | Default ESP32 I2C SDA |
| **GPIO 22** | OLED `SCL` | Default ESP32 I2C SCL |
| **GPIO 16 (RX2)** | Receiver `S.Port` Signal (via `1kΩ` resistor) | Inverted 57600 baud RX (`Serial2`) |
| **GPIO 17 (TX2)** | Optional `4.7kΩ` resistor to `S.Port` | Passive listen-only by default (`Serial2`) |
| **GPIO 0 (BOOT)** | Onboard BOOT Button to `GND` | Cycles OLED presets |

*(Note: A secondary `lolin_c3_pico` environment is also preserved in `platformio.ini` using pins SDA=8, SCL=10, RX=20, TX=21, BTN=9).*

---

## Quick Start (PlatformIO)
1. Build and upload the LittleFS filesystem image (`data/index.html`):
   ```bash
   pio run -e esp32dev --target uploadfs
   ```
2. Build and upload the firmware:
   ```bash
   pio run -e esp32dev --target upload
   ```
3. Connect to Wi-Fi SSID **`SPort-HUD`** (password: **`12345678`**) and open **`http://192.168.4.1`**.
