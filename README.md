# ESP32 Telemetry Multitool — v2.3.0

A direct-connect RC telemetry reader, 128×64 OLED HUD and live engineering web
dashboard for the **ESP-WROOM-32 (`esp32dev`)** (and optionally **LOLIN C3 Pico**)
with a 0.96" SSD1306 OLED.

It connects directly to a FrSky S.Port bus **without signal-inversion hardware**,
decodes telemetry from an **openXsensor** (and any standard FrSky sensor), hosts a
Wi-Fi access point (`SPort-HUD`) + WebSocket server, and drives a local OLED plus a
refined cockpit web UI — including a real artificial horizon with pitch ladder,
bank scale and heading tape.

---

## What’s new in v2.3.0 — CYD port

| Area | Highlights |
| --- | --- |
| **NEW build target** | `[env:cyd]` for the **ESP32-2443S028R “Cheap Yellow Display”**: 2.8″ ILI9341 320×240 color TFT cockpit, XPT2046 touch, RGB status LED. The classic `esp32dev` (OLED) build is unchanged. |
| **Five TFT pages** | Jet HUD · PFD (sky/ground + tapes) · Drivetrain (V/A/W, cells, sag, mΩ) · GPS map · Diagnostics — touch tab bar + BOOT button, colour/touch self-test. |
| **Pin changes on CYD** | S.Port RX = **GPIO 22**, TX = **GPIO 27** (GPIO 16/17 drive the onboard RGB LED). |

**CYD quick start**

```bash
pio run -e cyd --target uploadfs
pio run -e cyd --target upload
```

Wiring: S.Port signal → 1 kΩ → **GPIO 22** (JST P3/CN1); bench-master/MSP TX →
330 Ω–1 kΩ → **GPIO 27** (JST CN1); oXs 5 V from **P1 VIN**, common ground from
CN1/P3. Status LED: **green** = receiver link, **blue** = bench master, **red** =
no signal. The external SSD1306 is not used on this board (the web OLED-mirror card
shows “N/A — display is the TFT”). Full pin map, variant warning and the step log
live in **[docs/CYD_PORT.md](docs/CYD_PORT.md)**.

### Previous: v2.2.1

| Area | Highlights |
| --- | --- |
| **GPS fixed** | Latitude/longitude now decode per the real FrSky convention: both share app ID `0x0800`, longitude marked by bit 31, sign in bit 30; `0x0820` is GPS altitude; `0x0850` is UTC date/time (ignored). The map and coordinates show the true position. |
| **All displays agree** | The OLED mini-ADI rotated the horizon opposite to the web ADI (mirrored tilt). All displays now use the standard convention (bank right → horizon right side up), so the invert switches flip everything together. |
| **True OLED mirror** | The ESP32 streams its real 1024-byte framebuffer over the WebSocket (~4 fps, binary); the web mirror is pixel-exact — fonts, layouts, themes, flip, even the test patterns. The JS replica is only the DEMO fallback. |
| **Faithful Gripen HUD** | Ten-dot dials ported exactly from the author's Lua widget (modulo-100 pointer, sub-text below, edge positions), the sliding compass tape, and the STALL SPEED / PULL UP alert banner with configurable thresholds. |
| **Bench-bus guidance** | Decision tree for real sensors on the bench bus: resistor swap first, 2N7000 driver stage if needed (schematic below). |

### v2.2.0 — Instruments

Theme-specific instruments (Gripen HUD / PFD / G1000+HSI / automotive / marine), OLED preset 4 following the theme, voltage sag + internal resistance, inverted-flight support, WebSocket recovery, logger capacity.

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
| GPS | `0x0800` (lat **and** lon, bit31 = lon), `0x0820` alt, `0x0830` speed, `0x0840` course | Position, speed, course, GPS altitude (`0x0850` = UTC date/time, ignored) |
| Receiver link | `0xF101` | RSSI |

Notes:

- Values in the ACC slots are **0.1° units**; axis signs can be inverted in the web UI
  (Settings → Attitude Input) if the MPU6050 is mounted upside-down.
- Heading priority: **GPS course** while fresh → otherwise **IMU yaw** (gyro heading,
  drifts slowly without a magnetometer). The live badge shows which source is active.
- If you enabled `GPS_SPEED_IN_KMH` in oXs, switch *Settings → GPS speed encoding*.
- Cell frames are only sent by an oXs that measures cells (`NUMBEROFCELLS > 0`) or by
  a real FrSky FLVSS/MLVSS sensor on the same bus.

### Attitude quality — use the openXsensor DMP build

The attitude shown on the dashboard is computed **by openXsensor**, not by the
ESP32. Current oXs master derives roll/pitch from the MPU6050 **DMP quaternion**
(gyro-integrated with gravity correction), which is stable enough for a flight HUD.
Older oXs builds used a raw accelerometer tilt that swings with any linear
acceleration. If your horizon reacts to shaking/sliding the *level* board, update
the oXs:

1. **Wire the MPU6050 `INT` pin to the Pro Mini `D2` or `D3`** (the DMP needs the
   interrupt line; SDA/SCL/Vcc/GND stay as they are).
2. Flash the current master from `github.com/openXsensor/openXsensor` with
   `A_MPU6050_IS_CONNECTED YES`, keeping the ACC mapping above.
3. Run the accelerometer-offset calibration once (the `DISPLAY_ACC_OFFSET`
   procedure in `oXs_config_description.h`).
4. Optional on a GY-86 (HMC5883 present): `CALCULATE_YAW_WITH_HMC5883 YES` after
   the documented magnetometer calibration, for a real compass heading.

**Inverted flight — required oXs patch.** The stock formulas use `atan()`, which caps
roll at ±90°: going inverted reads as level flight. Patch `oXs_imu.cpp` (the
`yawCount >= 20` block) to the aviation convention:

```c
pitch.value = 10.0f * atan2(-gravity.x, sqrt(gravity.y*gravity.y + gravity.z*gravity.z))
              * radians_to_degrees;   // +-90 deg, nose-up positive
roll.value  = 10.0f * atan2(gravity.y, gravity.z)
              * radians_to_degrees;   // +-180 deg, right-bank positive
```

Rebuild the oXs, then verify on the bench: roll past vertical and the instrument must
read ~180°, not snap back. If pitch/roll are mirrored for your mounting, use
*Settings → Attitude input → invert* instead of editing the patch again.

**Limits:** no 6-DOF MEMS AHRS can ignore sustained acceleration. In a long turn the
horizon leans toward apparent gravity — real aircraft attitude indicators behave the
same way, only slower. Tune *Settings → Attitude smoothing* to taste; heading prefers
GPS course while moving and falls back to the IMU yaw.

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

> **Bench bus with a real sensor (FLVSS, current sensor…):** the receiver drives the
> S.Port line with a strong push-pull stage; the ESP32 master drives it through the
> GPIO17 series resistor. Adding a real sensor loads the line and can collapse the
> frame rate (polls ≈100/s but responses near zero on the Bus tab). Fix order:
>
> 1. **Quick test:** replace the 4.7 kΩ series resistor with **470 Ω – 1 kΩ**.
> 2. **Robust fix:** add a single-wire driver — 2N7000 open-drain stage:
>
> ```
> GPIO17 ──[ 470Ω ]──┬── Gate (2N7000)
>                    │
>                  [10kΩ]  pull-up to S.Port V+ (5V)
>                    │
> S.Port ────────────┴── Drain (2N7000), Source → GND
> ```
>
>    The MOSFET inverts the GPIO signal (S.Port is inverted UART), the pull-up
>    matches a receiver's line levels, and the drain only ever pulls the line low —
>    no contention with sensors' own drivers.

### Voltage sources

- `VFAS` (`0x0210`) is only transmitted when the oXs config defines
  `#define VFAS_SOURCE VOLT_1` (or another input). Add that line for direct pack
  measurement.
- When no VFAS is present, the firmware **derives pack voltage from the FLVSS cell
  frames** — a real voltage sensor alone drives pack + per-cell displays on web and
  OLED.
- Drivetrain diagnostics: the firmware tracks a no-load cell reference and reports
  **voltage sag** (cell/pack) plus an estimated **internal resistance (mΩ)**.

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
- **Bus & Logger** — packet rate, CRC errors, last app/phys IDs, bus role, polls/s,
  attitude source; browser logger (start/stop/rate, capacity 20k or 50k rows, CSV +
  JSON + **GPX** export). The live stream box shows the newest 80 lines; the ring
  buffer is the actual log (≈ 67 min at 5 Hz / 20k rows, ≈ 5.5 h at 1 Hz).
- **GPS / Map** — offline live track plot: auto home at first fix (+ “Set home here”),
  breadcrumb trail, distance rings, north-up / track-up, scale bar; position,
  distance/bearing from home, GPS vs baro altitude, ground speed and course.
- **Settings** — theme, OLED flip + slot layout, protocol, bus role, cell count,
  GPS speed encoding, attitude slot mapping + sign inversions + smoothing, OLED test
  patterns, apply/save to ESP32.

### Themes and instruments

| Theme | Instrument |
| --- | --- |
| **A1 — Fighter Jet HUD** | Gripen-style HUD ported from the author's ETHOS widget: hemispherical pitch ladder (solid zenith circles, dashed nadir circles with cardinal triangles), N/E/S/W crosses at both poles, boresight ghost, live flight-path vector, sliding compass tape, ten-dot speed/altitude dials (modulo-100 pointer, sub-text below), and blinking STALL SPEED / PULL UP alerts with configurable thresholds. |
| **A2 — Airliner PFD** | Filled attitude ball, pitch ladder with negative-pitch chevrons, ±60° bank scale, speed/altitude tapes, heading tape. |
| **A3 — GA / G1000** | Garmin-style PFD: attitude ball + tapes and a rotating **HSI compass rose** with cardinal/decade labels. |
| **B — Automotive cluster** | No attitude ball: central needle speedometer, kW and amp gauges, battery/ESC/motor bars, compass strip. |
| **C — Marine console** | 2-D navigation first: detailed compass rose, stern-view **heel** schematic and side-view **pitch** schematic, speed in km/h + knots. |

The **OLED preset 4** mirrors the active theme (jet HUD / ADI / speedo / marine).

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
{"type":"cfg","ver":"2.2.1","ssid":"SPort-HUD","ip":"192.168.4.1",
 "theme":"A1","preset":0,"protocol":0,"cells":0,"flipOled":false,
 "oledSlots":["VOLT","CURR","ALT","SPD"],"attMap":[1,2,3],
 "invPitch":false,"invRoll":false,"invYaw":false,
 "busMode":0,"gpsUnit":0,"attSmooth":70,"stallSpd":35,"minAlt":30,
 "heap":214000,"uptime":42}
```

Theme and preset travel **only** in this config frame (pushed on connect and after
every change, including the physical BOOT button) — not in the telemetry stream.

Telemetry is pushed at ~15 Hz:

`type, ver, proto, volt, cellV, cells, minV, cellMin, cellv[], sagCell, sagPack,
restCell, resMohm, curr, peakI, pwr, peakW, mah, rpm, escT, motT, alt, maxAlt, vspd,
spd, maxSpd, hdg, roll, pitch, lat, lon, gpsAlt, sats, gpsFix, rssi, fps, crcErr,
lastApp, lastPhy, attSrc, hdgSrc, master, polls, pollsSec, busMode, heap, uptime`

Commands (browser → ESP32):

```json
{"cmd":"setConfig","theme":"A2","protocol":0,"cells":0,"gpsUnit":0,"busMode":0,
 "flipOled":false,"oledSlots":["VOLT","CURR","ALT","SPD"],"attMap":[1,2,3],
 "invPitch":false,"invRoll":false,"invYaw":false,"attSmooth":70,
 "stallSpd":35,"minAlt":30}
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
| Bench master only reaches a few FPS | Fixed in v2.1.0 (polling task + adaptive rotation); make sure both firmware and `index.html` are updated |
| Theme/preset selection snaps back | Fixed in v2.1.0 (theme/preset removed from the 15 Hz telemetry stream); hard-refresh after flashing |
| GPS tab says “Waiting for GPS fix” | oXs needs `A_GPS_IS_CONNECTED YES` and a 3D fix outdoors; check the Bus & Logger app IDs for `0x0800/0x0820` |
| Web page won’t open | Connect to `SPort-HUD` first; try `http://192.168.4.1`; the captive portal opens on most phones |
| UI stale after flashing a new `index.html` | Hard-refresh the browser (page is served `no-store`; the WS reconnects automatically) |

---

## Project layout

```
platformio.ini          # esp32dev + lolin_c3_pico environments
scripts/gzip_web.py     # auto-gzip the web UI before buildfs/uploadfs
src/main.cpp            # Wi-Fi AP, WebSocket, mDNS/DNS, config, OLED loop
src/SportParser.h       # S.Port decoder + bench master polling task
src/TelemetryBus.h      # Data model & enums
src/OledRenderer.h      # 4 OLED presets + self-test patterns
src/ProtocolParser.h    # Parser interface (MSP/MAVLink/CRSF/Jeti stubs in FutureParsers.h)
data/index.html         # Single-file cockpit dashboard (no external assets)
```

## Credits

- [openXsensor](https://github.com/openXsensor/openXsensor) — S.Port frame layout,
  ACC-slot attitude mapping and cell/GPS encodings were verified against its source.
- [gripen-HUD-for-ETHOS-X12S](https://github.com/guerradr/gripen-HUD-for-ETHOS-X12S) —
  the A1 Gripen HUD symbology (same author).
- FrSky S.Port protocol documentation & community reverse-engineering.

## License

This project is licensed under the **GNU General Public License v3.0** — see
[LICENSE](LICENSE). The GPLv3 applies because the A1 instrument ports symbology and
code structure from the author's GPLv3 Gripen HUD widget.
