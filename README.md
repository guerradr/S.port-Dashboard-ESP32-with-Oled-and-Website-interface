# ESP32 Telemetry Multitool — v2.0.0 “Cockpit Edition”

A direct-connect RC telemetry reader, 128×64 OLED HUD and live engineering web
dashboard for the **ESP-WROOM-32 (`esp32dev`)** (and optionally **LOLIN C3 Pico**)
with a 0.96" SSD1306 OLED.

It connects directly to a FrSky S.Port bus **without signal-inversion hardware**,
decodes telemetry from an **openXsensor** (and any standard FrSky sensor), hosts a
Wi-Fi access point (`SPort-HUD`) + WebSocket server, and drives a local OLED plus a
refined cockpit web UI — including a real artificial horizon with pitch ladder,
bank scale and heading tape.

---

## What’s new in v2.0.0

| Area | Highlights |
| --- | --- |
| **Artificial horizon** | Full PFD-style instrument: pitch ladder with degree labels, bank scale ±60° with rolling pointer, fixed aircraft symbol, heading tape, vario chevrons, digital readouts. PFD speed/altitude tapes in theme A2. |
| **Real attitude data** | Decodes the openXsensor ACC slots `0x0700/0x0710/0x0720` (PITCH / ROLL / YAW, 0.1° units) with configurable mapping and sign inversion. |
| **FLVSS cells** | Decodes `0x0300` cell frames → real per-cell voltages, lowest cell and true cell count (voltage guess is now only a fallback). |
| **GPS** | Decodes lat/lon (`0x0800/0x0820`), speed (`0x0830`), course (`0x0840`) and GPS altitude (`0x0850`). |
| **Bench master mode** | With no receiver connected, the ESP32 becomes the S.Port bus master and polls sensors itself — the multitool works standalone on the bench (auto-detect, or force it). |
| **OLED** | Redesigned dense layouts (no more dark bands between rows), 4th attitude preset, explicit 400 kHz I²C, 10 fps dirty-flag rendering, and built-in test patterns. |
| **Web UI** | Auto-connect/reconnect, status LED, stale-data watchdog, two-way config sync, chart axes/legends/hover, logger with CSV/JSON export, toast feedback, theme-aware cockpit styling. |
| **Server** | mDNS `sport-hud.local`, captive-portal DNS so phones open the dashboard automatically, optional gzip-compressed UI. |
| **Toolchain** | Migrated to `ESP32Async/ESPAsyncWebServer` v3.6.x + `ESP32Async/AsyncTCP`. |

See [CHANGELOG.md](CHANGELOG.md) for the full list.

---

## Hardware wiring (ESP-WROOM-32 / `esp32dev`)

> **3.3 V logic warning:** ESP32 GPIOs are **3.3 V** (not 5 V tolerant). Power the
> board from a regulated 5 V BEC/rx rail on `VIN`/`5V`, and place a **1 kΩ – 2.2 kΩ**
> series resistor on the S.Port signal line to `GPIO 16`.

| ESP-WROOM-32 pin | Connect to | Notes |
| --- | --- | --- |
| **VIN / 5V** | Receiver servo rail `+` (5 V) | Powers the onboard 3.3 V regulator |
| **GND** | Receiver `-`, OLED `GND`, oXs `GND` | Common ground |
| **3V3** | OLED `VCC` | 3.3 V OLED supply |
| **GPIO 21** | OLED `SDA` | Default ESP32 I²C SDA |
| **GPIO 22** | OLED `SCL` | Default ESP32 I²C SCL |
| **GPIO 16 (RX2)** | Receiver `S.Port` signal via 1 kΩ | Inverted 57600 baud RX (`Serial2`) |
| **GPIO 17 (TX2)** | `S.Port` via **4.7 kΩ** | Used only in **bench master mode** to poll sensors; idles harmlessly otherwise |
| **GPIO 0 (BOOT)** | Onboard BOOT button | Short press: next OLED preset · long press (>1.2 s): OLED self-test |

The `lolin_c3_pico` environment is preserved in `platformio.ini`
(SDA=8, SCL=10, RX=20, TX=21, BTN=9).

---

## openXsensor integration

The ESP32 expects the **openXsensor S.Port output** (physical sensor IDs 0x00, 0xA1,
0x22, 0x83, 0xE4, 0x67 are polled in the standard rotation). Recommended oXs settings:

```c
// oXs_config_basic.h — attitude
#define ACCX_SOURCE PITCH      // -> S.Port 0x0700
#define ACCY_SOURCE ROLL       // -> S.Port 0x0710
#define ACCZ_SOURCE YAW        // -> S.Port 0x0720

// GPS (if fitted)
#define A_GPS_IS_CONNECTED YES
// #define GPS_SPEED_IN_KMH    // leave disabled (default = knots ×1000)
```

| oXs data | S.Port app ID | Dashboard use |
| --- | --- | --- |
| Baro altitude / vario | `0x0100` / `0x0110` | Altitude, vertical speed, vario chevrons |
| VFAS / current / fuel | `0x0210` / `0x0200` / `0x0600` | Pack voltage, current, power |
| FLVSS cells | `0x0300` | Per-cell voltages, minimum cell, cell count |
| Temperatures | `0x0400` / `0x0410` | ESC / motor temperature |
| RPM | `0x0500` | RPM |
| **ACC slots** | **`0x0700/0x0710/0x0720`** | **Pitch / roll / yaw (0.1°)** |
| GPS | `0x0800/0x0820/0x0830/0x0840/0x0850` | Position, speed, course, GPS altitude |
| Receiver link | `0xF101` | RSSI |

Notes:

- Values in the ACC slots are **0.1° units**; axis signs can be inverted in the web UI
  (Settings → Attitude Input) if the MPU6050 is mounted upside-down.
- Heading priority: **GPS course** while fresh → otherwise **IMU yaw** (gyro heading,
  drifts slowly without a magnetometer). The live badge shows which source is active.
- If you enabled `GPS_SPEED_IN_KMH` in oXs, switch *Settings → GPS speed encoding*.
- Cell frames are only sent by an oXs that measures cells (`NUMBEROFCELLS > 0`) or by
  a real FrSky FLVSS/MLVSS sensor on the same bus.

---

## Receiver compatibility

All receivers below act as S.Port bus masters and poll standard sensors, so the
oXs + ESP32 tap works unchanged.

| Receiver | S.Port access | What to do |
| --- | --- | --- |
| X-series (X4R, X6R, X8R, XR…) | Dedicated S.Port connector | Wire as in the table above |
| S6R / S8R (ACCST stabilized) | Dedicated S.Port connector | Works as-is |
| **Archer Plus SR6 Mini** | Dedicated S.Port / F.Port / FBUS port | Set the port to **S.Port** (not F.Port/FBUS) |
| **Archer Plus SR6 Lite** | Function ports (SBUS In/Out, S.Port/FBUS) | Set the function port to **S.Port** |
| **Archer Plus SR12+** | Any of the 12 channel ports can be assigned **S.Port** | Assign one channel port to S.Port (the side 4-pin connector is *not* S.Port on this model) |

> **Important:** if a configurable port is set to **F.Port/FBUS**, the bus carries
> F.Port frames and this S.Port parser will see nothing. Keep legacy **S.Port** mode.

**Stabilized receivers** keep working with external sensors: the stabilizer uses its
internal gyro and does not consume the S.Port bus.

---

## Bench master mode (no receiver required)

S.Port sensors are polled slaves — with no receiver on the bus they stay silent.
The firmware can take over the master role:

- **Auto (default):** if the bus is silent for 2 s, the ESP32 starts polling the
  standard physical-ID rotation. If a real receiver appears (its polling is detected),
  the ESP32 instantly yields the bus back and waits 2 s before retrying.
- **Receiver only:** always passive (flight configuration).
- **Bench master:** always poll — useful for bench debugging and sensor bring-up.

Selection lives in *Settings → Telemetry Bus → Bus role* and is stored in flash.
On the bench: power the oXs from a 5 V source (no receiver rail), connect
oXs signal + ESP32 tap to the same S.Port wire. RSSI will read 0/– and the UI shows
**BENCH MASTER** — expected, as there is no link.

---

## Build & upload (PlatformIO)

```bash
# 1. Filesystem image (web UI)
pio run -e esp32dev --target uploadfs

# 2. Firmware
pio run -e esp32dev --target upload
```

Then connect to Wi-Fi **`SPort-HUD`** (password **`12345678`**) and open
**http://192.168.4.1** (or **http://sport-hud.local**). Any HTTP request is
redirected to the dashboard by the captive portal.

The filesystem build automatically compresses the UI (`scripts/gzip_web.py`,
~18 KB instead of ~67 KB). The firmware serves `/index.html.gz` with
`Content-Encoding: gzip` and falls back to `/index.html` if the compressed file
is absent.

---

## Web dashboard

- **Live HUD** — artificial horizon, OLED mirror, pack voltage/current, altitude/speed
  charts (axes, legends, hover crosshair), KPI strip.
- **EDF / Drivetrain** — power, RPM, temperatures, consumption and load charts.
- **Bus & Logger** — packet rate, CRC errors, last app/phys IDs, bus role, attitude
  source; browser logger (start/stop/rate, CSV + JSON export).
- **Settings** — theme, OLED flip + slot layout, protocol, bus role, cell count,
  GPS speed encoding, attitude slot mapping + sign inversions, OLED test patterns,
  apply/save to ESP32.

Connection handling: the page connects automatically, reconnects with backoff and
shows a status LED (`LIVE` / `SIM` / `CONNECTING` / `NO SIGNAL`). **DEMO** mode
simulates a full flight for demos and is disabled automatically by live data.

### OLED presets

1. **Info** — pack voltage (large), cells/RSSI, 4 configurable slots (large values)
2. **EDF** — power, current, RPM, peaks, ESC temperature + cell voltage bars
3. **Debug** — packet rate, CRC, app/phys IDs, raw value, active sensors, bus role
4. **Attitude** — mini HUD: heading, horizon + pitch ladder, roll/pitch, altitude/speed

Press **BOOT** briefly to cycle presets. Hold **BOOT** for >1.2 s to enter the OLED
self-test (white → black → checkerboard → horizontal lines → vertical lines → cross);
short presses advance the pattern; it exits automatically after 60 s (or hold again).

### OLED “black horizontal lines”

The v2 layouts fill all 64 rows, so bands between text rows are gone. If you still
see **permanent** dark lines, run the self-test patterns:

- If lines persist on the **white** pattern, the panel/ribbon is faulty (hardware).
- If they only appear while content updates, check I²C wiring/pull-ups; the firmware
  runs the bus at 400 kHz and renders at max 10 fps.

---

## WebSocket protocol (v2)

The UI speaks JSON over `ws://<ip>/ws`.

On connect the ESP32 pushes a **config** frame:

```json
{"type":"cfg","ver":"2.0.0","ssid":"SPort-HUD","ip":"192.168.4.1",
 "theme":"A1","preset":0,"protocol":0,"cells":0,"flipOled":false,
 "oledSlots":["VOLT","CURR","ALT","SPD"],"attMap":[1,2,3],
 "invPitch":false,"invRoll":false,"invYaw":false,
 "busMode":0,"gpsUnit":0,"heap":214000,"uptime":42}
```

Telemetry is pushed at ~15 Hz:

`type, ver, proto, preset, theme, volt, cellV, cells, minV, cellMin, cellv[], curr,
peakI, pwr, peakW, mah, rpm, escT, motT, alt, maxAlt, vspd, spd, maxSpd, hdg, roll,
pitch, lat, lon, gpsAlt, sats, gpsFix, rssi, fps, crcErr, lastApp, lastPhy, attSrc,
hdgSrc, master, polls, busMode, heap, uptime`

Commands (browser → ESP32):

```json
{"cmd":"setConfig","theme":"A2","protocol":0,"cells":0,"gpsUnit":0,"busMode":0,
 "flipOled":false,"oledSlots":["VOLT","CURR","ALT","SPD"],"attMap":[1,2,3],
 "invPitch":false,"invRoll":false,"invYaw":false}
{"cmd":"resetPeaks"}
{"cmd":"oledTest","pattern":0}
{"cmd":"getCfg"}
```

---

## Troubleshooting

| Symptom | Check |
| --- | --- |
| No packets, OLED shows `BUS NO SIGNAL` | S.Port wire on GPIO 16, common ground, 57600 inverted; receiver port set to S.Port (not F.BUS); oXs configured for SPORT |
| Pack voltage/current OK, no attitude | `ACCX/Y/Z_SOURCE` set to PITCH/ROLL/YAW in oXs; attitude mapping matches in Settings; signs inverted for your mounting |
| Roll/pitch reversed | Settings → Attitude Input → invert roll/pitch |
| Speed 5.4× too low/high | Settings → GPS speed encoding (knots vs km/h) |
| RSSI always 0 | No receiver link (bench master) — expected |
| Web page won’t open | Connect to `SPort-HUD` first; try `http://192.168.4.1`; the captive portal opens on most phones |
| UI stale after flashing a new `index.html` | Hard-refresh the browser (page is served `no-store`; the WS reconnects automatically) |

---

## Project layout

```
platformio.ini          # esp32dev + lolin_c3_pico environments
src/main.cpp            # Wi-Fi AP, WebSocket, mDNS/DNS, config, OLED loop
src/SportParser.h       # S.Port decoder + bench master polling
src/TelemetryBus.h      # Data model & enums
src/OledRenderer.h      # 4 OLED presets + self-test patterns
src/ProtocolParser.h    # Parser interface (MSP/MAVLink/CRSF/Jeti stubs in FutureParsers.h)
data/index.html         # Single-file cockpit dashboard (no external assets)
```

## Credits

- [openXsensor](https://github.com/openXsensor/openXsensor) — S.Port frame layout,
  ACC-slot attitude mapping and cell/GPS encodings were verified against its source.
- FrSky S.Port protocol documentation & community reverse-engineering.
