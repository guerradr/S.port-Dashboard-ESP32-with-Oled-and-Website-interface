# CYD Port — ESP32-2443S028R (Cheap Yellow Display)

**Project:** S.Port Dashboard ESP32 Multitool, v2.3.0
**Status:** firmware complete and building; hardware wiring/flash pending
**Purpose:** single source of truth for the CYD port.

---

## 1. Hardware requirements

| Requirement | ESP32-2443S028R | Project need | Verdict |
| --- | --- | --- | --- |
| MCU | ESP32-WROOM-32 | ESP32 dual-core | ✅ same core as `esp32dev` |
| Flash / RAM | 4 MB, 320 KB, no PSRAM | 1.09 MB app + 1.4 MB FS, ~51 KB RAM | ✅ 83.1 % flash, 15.7 % RAM |
| Display | 2.8" ILI9341 320×240 color, SPI | new primary display | ✅ TFT_eSPI 2.5.43 |
| Touch | XPT2046 resistive, own SPI | page navigation | ✅ PaulStoffregen/XPT2046_Touchscreen (git) |
| Wi-Fi AP + WS | built-in | `SPort-HUD` + WebSocket | ✅ unchanged |
| Free GPIOs | 22 (P3/CN1), 27 (CN1), 35 (in-only) | S.Port RX+TX | ✅ RX=22, TX=27 |
| USB | CH340G micro-USB | flash/log | ✅ (no device attached at build time) |
| LEDs | RGB 4/16/17 active LOW | status | ✅ green link / blue master / red none |
| Speaker / LDR / SD | 26 / 34 / 5+18+19+23 | future options | 🔵 reserved |

### Variant check (on delivery — still pending)
- [ ] Single micro-USB, silkscreen ESP32-2443S028R / 2432S028R family
- [ ] Resistive touch (soft film)
- [ ] If dual-USB or ST7789: change TFT flags (`ST7789_DRIVER` / `ILI9342_DRIVER`, inversion)

---

## 2. Pin map (as implemented)

| Function | GPIO | Notes |
| --- | --- | --- |
| TFT MOSI / MISO / SCLK / CS / DC | 13 / 12 / 14 / 15 / 2 | HSPI, `ILI9341_2_DRIVER`, RST=−1 (EN) |
| TFT backlight | 21 | `TFT_BACKLIGHT_ON=HIGH` (TFT_eSPI handles) |
| Touch CLK / MOSI / MISO / CS / IRQ | 25 / 32 / 39 / 33 / 36 | separate `SPIClass(VSPI)` |
| RGB LED R / G / B | 4 / 16 / 17 | active LOW |
| **S.Port RX2** | **22** | via 1 kΩ (P3 or CN1) |
| **S.Port TX2 (bench master / MSP)** | **27** | via 330 Ω–1 k (CN1; board 10 k pull-up) |
| oXs power | P1 VIN 5 V + GND | |
| Common ground | CN1/P3 GND | |

Why 16/17 moved: they drive the onboard RGB LED (green/blue). GPIO 1/3 (P1 TX/RX) are
shared with the CH340 and unusable for telemetry; GPIO 2 is TFT DC.

---

## 3. Implementation (done)

- `[env:cyd]` in `platformio.ini`: board `esp32dev`, TFT_eSPI via `USER_SETUP_LOADED`
  flags (no library-folder edits), XPT2046 from GitHub, gzip UI hook.
- `src/TftRenderer.h`: five pages, touch tab bar + BOOT, theme accent colours,
  GPS trail map, diagnostics incl. raw touch values, colour/touch self-test.
  Full-page redraw throttled to ~8 fps; render body ~30 ms at SPI 40 MHz.
- `main.cpp`: CYD pin block, RGB status LED, board field in `cfg`
  (`"board":"CYD"`), display block guarded from the OLED path.
- Web: `board` badge in Settings; OLED mirror card shows “CYD — display is the TFT”.
- Classic `esp32dev` (OLED) build unchanged and still green.

---

## 4. Build log

| Step | Result |
| --- | --- |
| `pio run -e cyd` | ✅ RAM 15.7 % (51 368 B), Flash 83.1 % (1 088 995 B) |
| `pio run -e esp32dev` | ✅ RAM 15.5 %, Flash 83.1 % (regression check) |
| `uploadfs` / `upload` (cyd) | ⏳ no serial device attached during the session |
| Hardware smoke test | ⏳ pending delivery |

### Findings during the build

1. `paulstoffregen/XPT2046_Touchscreen` exists in the registry only as an
   unversioned snapshot (`0.0.0-alpha+sha…`) → use the GitHub URL.
   The `tedtoal/…_TT` fork pulls Adafruit_GFX (TS_Display) → avoid.
2. TFT_eSPI warns “TOUCH_CS not defined” — intended: touch runs on its own bus
   and library, not through TFT_eSPI.
3. `XPT2046_Touchscreen.h` is resolved via `__has_include`, so both the original
   and the `_TT` fork compile.
4. Touch raw range assumed 200–3900; the DBG page prints raw/mapped values —
   adjust `TOUCH_RAW_MIN/MAX` (or swap axes) if corners don’t line up.

---

## 5. Multi-protocol readiness

| Protocol | Baud | Signal | Notes |
| --- | --- | --- | --- |
| FrSky S.Port | 57600 | inverted UART | current |
| MSP | 115200 | normal | same pins |
| MAVLink | 57600 | normal | same pins |
| CRSF / ELRS | 420000 | single-wire half-duplex | bridge RX/TX line ends with a jumper |
| Jeti EX | 125000 | normal | same pins |

Parser stubs already exist in `FutureParsers.h`; implementation is the v2.4
roadmap. Each parser configures baud/inversion in `begin()`.

---

## 6. Remaining steps (hardware)

- [ ] S0.1–S0.4 Variant check, TFT smoke test, touch corner test, JST pigtails
- [ ] S2.1 `pio run -e cyd --target uploadfs`
- [ ] S2.2 `pio run -e cyd --target upload`
- [ ] S2.3 Five pages render, touch + BOOT navigation
- [ ] S2.4 RX-link test on GPIO 22 (60 FPS)
- [ ] S2.5 Bench master on GPIO 27
- [ ] S2.6 RGB LED states
- [ ] S2.7 Self-test patterns + touch crosshair
- [ ] S3.1–S3.4 Wiring: 1 k → 22, 330 Ω–1 k → 27, 5 V/GND, CRSF bridge prep,
      bench-master drive check
- [ ] S4.3 Acceptance matrix

### Acceptance matrix
| Check | Pass |
| --- | --- |
| All 5 pages render ≤10 fps with live data | ☐ |
| Touch + BOOT navigate pages | ☐ |
| S.Port telemetry identical to esp32dev (RX link 60 FPS) | ☐ |
| Bench master works on GPIO 27 | ☐ |
| Web dashboard shows `board: CYD`, mirror card marked N/A | ☐ |
| RGB LED reflects link/master/no-signal | ☐ |
