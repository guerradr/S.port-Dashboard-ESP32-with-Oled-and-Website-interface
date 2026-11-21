# Changelog

All notable changes to this project are documented here.

## [2.0.0] — 2026-11-21 — “Cockpit Edition”

### Added

- **Real artificial horizon** (web): pitch ladder with degree labels, bank scale
  ±60° with rolling pointer, fixed aircraft symbol, heading tape, vario chevrons,
  digital roll/pitch/heading readouts and PFD-style speed/altitude tapes for theme A2.
- **openXsensor attitude decoding**: S.Port ACC slots `0x0700` (pitch), `0x0710`
  (roll), `0x0720` (yaw), 0.1° units, with configurable slot mapping and per-axis
  sign inversion (persisted in flash).
- **FLVSS cell decoding** (`0x0300`): real per-cell voltages, minimum cell and true
  cell count; the old voltage-based guess is now a fallback only.
- **GPS decoding**: latitude/longitude (`0x0800`/`0x0820`), speed (`0x0830`), course
  (`0x0840`) and GPS altitude (`0x0850`); GPS speed unit setting (knots ×1000 default,
  km/h ×100 option).
- **Bench master mode**: ESP32 polls the S.Port bus when no receiver is present.
  Auto (2 s silence → master, yields instantly to a real receiver), receiver-only and
  forced bench-master modes, selectable in the UI and stored in flash.
- **OLED preset 4 — attitude HUD**: mini horizon with pitch ladder, heading,
  roll/pitch and altitude/speed.
- **OLED self-test patterns** (white/black/checkerboard/lines/cross) via web UI or
  BOOT long-press, to separate panel defects from layout artefacts.
- **WebSocket protocol v2**: config echo (`cfg`) on connect, `hello` data
  (version/heap/uptime), extended telemetry fields (`attSrc`, `hdgSrc`, `master`,
  `polls`, `cellv[]`, `sats`, `gpsFix`, `maxAlt`, `maxSpd`, …), `getCfg` and
  `oledTest` commands.
- **Network polish**: mDNS `sport-hud.local`, captive-portal DNS redirect, optional
  gzip-compressed dashboard served from LittleFS.
- **Web UI v2**: auto-connect + exponential-backoff reconnect, status LED and
  stale-data watchdog, two-way config sync, toast feedback, chart axes/legends/hover
  crosshair, browser logger (start/stop/rate, CSV + JSON export), DPR-sharp canvases,
  responsive layout, favicon, firmware/IP/uptime/heap footer.

### Fixed

- **OLED horizontal black bands**: all presets redesigned to fill the full 64-row
  height with size-2 values; explicit 400 kHz I²C and 10 fps dirty-flag rendering
  remove tearing and dead rows.
- **Artificial horizon looked unfinished**: replaced the single rotated line with a
  complete instrument (degrees, bank scale, heading, reference symbol).
- **Settings never reached the browser**: the ESP32 now pushes its stored config on
  every WebSocket connection, and theme/preset changes broadcast to all clients.
- **CSV timestamp header** (`time_ms`) now matches the epoch-millisecond data, with
  an added ISO-8601 column.
- OLED manual cell-count override no longer gets overwritten by auto-detection.
- Package rate/FPS and diagnostics counters now remain correct in bench-master mode.

### Changed

- Libraries migrated to `ESP32Async/ESPAsyncWebServer @ ^3.6.0` and
  `ESP32Async/AsyncTCP @ ^3.3.3`.
- Presets expanded from 3 to 4 (BOOT button cycles all four).
- `SystemConfig` gained attitude mapping, sign inversions, bus role and GPS unit
  settings; the unused `customWebLayoutJson` field was removed.
- Binary version bumped to 2.0.0; README rewritten (oXs setup, receiver
  compatibility, bench mode, WS protocol).
