#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "TelemetryBus.h"

// ---------------------------------------------------------------------------
// OLED renderer (v2.0.0)
//
// Layout rules: every preset fills all 64 rows.  Values use the 12x16 (size 2)
// font wherever possible so there are no multi-pixel dark bands between rows.
// ---------------------------------------------------------------------------

class OledRenderer {
private:
    Adafruit_SSD1306 _display;

    // Format a metric into at most 5 size-2 characters (60 px).
    String formatMetric(const String& key, const TelemetryData& d) {
        char buf[16];
        if (key == "VOLT") {
            snprintf(buf, sizeof(buf), "%.1fV", d.totalVoltage);
        } else if (key == "CELL") {
            snprintf(buf, sizeof(buf), "%.2fV", d.cellVoltage);
        } else if (key == "CURR") {
            if (d.current >= 100.0f)
                snprintf(buf, sizeof(buf), "%.0fA", d.current);
            else
                snprintf(buf, sizeof(buf), "%.1fA", d.current);
        } else if (key == "PWR") {
            int w = (int)d.powerWatts;
            snprintf(buf, sizeof(buf), "%dW", w);
        } else if (key == "MAH") {
            if (d.mahUsed >= 10000.0f)
                snprintf(buf, sizeof(buf), "%.1fk", d.mahUsed / 1000.0f);
            else
                snprintf(buf, sizeof(buf), "%.0fm", d.mahUsed);
        } else if (key == "RPM") {
            int r = (int)d.rpm;
            if (r >= 100000)
                snprintf(buf, sizeof(buf), "%dk", r / 1000);
            else
                snprintf(buf, sizeof(buf), "%d", r);
        } else if (key == "TEMP") {
            snprintf(buf, sizeof(buf), "%dC", (int)d.escTemp);
        } else if (key == "ALT") {
            snprintf(buf, sizeof(buf), "%dm", (int)d.altitude);
        } else if (key == "SPD") {
            int s = (int)d.speedKmh;
            if (s >= 1000)
                snprintf(buf, sizeof(buf), "%.0fk", (float)s / 1000.0f);
            else
                snprintf(buf, sizeof(buf), "%dkmh", s);
        } else if (key == "RSSI") {
            snprintf(buf, sizeof(buf), "%ddB", d.rssi);
        } else {
            snprintf(buf, sizeof(buf), "--");
        }
        return String(buf);
    }

    // ------------------------------------------------------------------
    // Preset 1: Flight info.  Header (size 2 pack voltage) + 2x2 slots.
    // ------------------------------------------------------------------
    void drawInfoPreset(const TelemetryData& d, const SystemConfig& cfg) {
        _display.setTextSize(2);
        _display.setCursor(0, 0);
        _display.printf("%.1fV", d.totalVoltage);

        _display.setTextSize(1);
        _display.setCursor(66, 0);
        _display.printf("%dS %.2fV", (int)d.detectedCells, d.cellVoltage);
        _display.setCursor(66, 8);
        _display.printf("RS %ddB", d.rssi);

        _display.drawFastVLine(62, 16, 48, SSD1306_WHITE);

        for (int i = 0; i < 4; i++) {
            int x = (i % 2) * 64 + 1;
            int y = 16 + (i / 2) * 24;
            _display.setTextSize(1);
            _display.setCursor(x, y);
            _display.print(cfg.customOledSlots[i]);
            _display.setTextSize(2);
            _display.setCursor(x, y + 8);
            _display.print(formatMetric(cfg.customOledSlots[i], d));
        }
    }

    // ------------------------------------------------------------------
    // Preset 2: EDF / drivetrain.  Big watts, dense text rows, two bars.
    // ------------------------------------------------------------------
    void drawEdfPreset(const TelemetryData& d) {
        _display.setTextSize(1);
        _display.setCursor(0, 0);
        _display.printf("EDF %dS %dmAh", (int)d.detectedCells, (int)d.mahUsed);

        _display.setTextSize(2);
        _display.setCursor(0, 8);
        _display.printf("%dW", (int)d.powerWatts);

        _display.setTextSize(1);
        _display.setCursor(66, 8);
        _display.printf("%.1fA", d.current);
        _display.setCursor(66, 16);
        _display.printf("%.1fV", d.totalVoltage);

        _display.setCursor(0, 24);
        _display.printf("RPM %d  MIN %.1fV", (int)d.rpm, d.minVoltage);
        _display.setCursor(0, 32);
        _display.printf("SAG %.2fV PK %dW", d.sagPackV, (int)d.peakWatts);

        _display.drawFastHLine(0, 40, 128, SSD1306_WHITE);

        // ESC temperature bar (20..100 C)
        _display.setCursor(0, 42);
        _display.printf("ESC%dC", (int)d.escTemp);
        _display.drawRect(48, 41, 80, 11, SSD1306_WHITE);
        int w1 = constrain(map((int)d.escTemp, 20, 100, 0, 76), 0, 76);
        _display.fillRect(50, 43, w1, 7, SSD1306_WHITE);

        // Cell voltage bar (3.0..4.25 V)
        _display.setCursor(0, 54);
        _display.print("CELL");
        _display.drawRect(48, 53, 80, 11, SSD1306_WHITE);
        int mv = (int)(d.cellVoltage * 100);
        int w2 = constrain(map(mv, 300, 425, 0, 76), 0, 76);
        _display.fillRect(50, 55, w2, 7, SSD1306_WHITE);
    }

    // ------------------------------------------------------------------
    // Preset 3: Bus & sensor diagnostics.
    // ------------------------------------------------------------------
    void drawDebugPreset(const TelemetryData& d, const char* protoName) {
        _display.setTextSize(1);
        _display.setCursor(0, 0);
        _display.print(protoName);
        _display.drawFastHLine(0, 9, 128, SSD1306_WHITE);

        _display.setCursor(0, 11);
        _display.printf("FPS %3u  CRC %lu", (unsigned)d.packetsPerSec,
                        (unsigned long)d.crcErrors);
        _display.setCursor(0, 19);
        _display.printf("APP 0x%04X PHY 0x%02X", (unsigned)d.lastAppId,
                        (unsigned)d.lastPhysId);
        _display.setCursor(0, 27);
        _display.printf("PKT %lu", (unsigned long)d.validPackets);
        _display.setCursor(0, 35);
        _display.printf("RAW 0x%08lX", (unsigned long)d.lastRawValue);

        _display.drawFastHLine(0, 43, 128, SSD1306_WHITE);

        _display.setCursor(0, 45);
        _display.print("ACT:");
        if (d.activeSensorMask & (1 << 0)) _display.print("V");
        if (d.activeSensorMask & (1 << 1)) _display.print("I");
        if (d.activeSensorMask & (1 << 2)) _display.print("R");
        if (d.activeSensorMask & (1 << 3)) _display.print("T");
        if (d.activeSensorMask & (1 << 5)) _display.print("A");
        if (d.activeSensorMask & (1 << 7)) _display.print("G");
        if (d.activeSensorMask & (1 << 9)) _display.print("M");
        if (d.activeSensorMask & (1 << 10)) _display.print("C");

        _display.setCursor(0, 53);
        if (d.busMasterActive) {
            _display.print("BUS BENCH MASTER");
        } else if (d.packetsPerSec > 0 || d.busRxSeen) {
            _display.print("BUS RX LINK");
        } else {
            _display.print("BUS NO SIGNAL");
        }

        // Packet-rate meter fills the last 4 rows.
        int w = constrain(map((int)d.packetsPerSec, 0, 120, 0, 128), 0, 128);
        _display.fillRect(0, 62, w, 2, SSD1306_WHITE);
    }

    // ------------------------------------------------------------------
    // Preset 4: theme-following instrument mini view.
    // A1 = jet HUD (Gripen-lite), A2/A3 = ADI ladder, B = car speedo,
    // C = marine compass + heel/pitch.  Falls back to the ADI ladder.
    // ------------------------------------------------------------------
    void drawAttitudePreset(const TelemetryData& d, const String& theme) {
        if (theme == "A1") { drawJetHudMini(d); return; }
        if (theme == "B")  { drawSpeedoMini(d); return; }
        if (theme == "C")  { drawMarineMini(d); return; }
        drawAdiMini(d);
    }

    void drawAdiMini(const TelemetryData& d) {
        unsigned long now = millis();

        _display.setTextSize(1);
        _display.setCursor(0, 0);
        _display.printf("HDG %03d", ((int)(d.heading + 360.0f)) % 360);
        _display.setCursor(98, 0);
        _display.print(attSourceLabel(d, now));

        const int cx = 64, cy = 27;
        const float kDeg2Rad = 0.0174532925f;
        bool attFresh = d.imuAttLastMs && (now - d.imuAttLastMs) < 2000;
        float roll = (attFresh ? d.roll : 0.0f) * kDeg2Rad;
        float pitch = attFresh ? d.pitch : 0.0f;
        float cr = cosf(roll), sr = sinf(roll);
        const float pxd = 1.35f;

        auto px = [&](float u, float v) -> int { return (int)(cx + u * cr + v * sr); };
        auto py = [&](float u, float v) -> int { return (int)(cy - u * sr + v * cr); };

        // Pitch ladder (every 5 deg, longer every 10 deg).
        for (int deg = -30; deg <= 30; deg += 5) {
            if (deg == 0) continue;
            float v = (pitch - (float)deg) * pxd;
            if (v < -19.0f || v > 19.0f) continue;
            float half = (deg % 10 == 0) ? 17.0f : 9.0f;
            _display.drawLine(px(-half, v), py(-half, v), px(half, v), py(half, v),
                              SSD1306_WHITE);
        }

        // Horizon line (2 px thick) at zero pitch.
        float v0 = pitch * pxd;
        _display.drawLine(px(-70, v0), py(-70, v0), px(70, v0), py(70, v0), SSD1306_WHITE);
        _display.drawLine(px(-70, v0 + 1), py(-70, v0 + 1), px(70, v0 + 1), py(70, v0 + 1),
                          SSD1306_WHITE);

        // Fixed aircraft symbol.
        _display.drawFastHLine(cx - 14, cy, 10, SSD1306_WHITE);
        _display.drawFastHLine(cx + 5, cy, 10, SSD1306_WHITE);
        _display.drawFastVLine(cx - 14, cy, 2, SSD1306_WHITE);
        _display.drawFastVLine(cx + 14, cy, 2, SSD1306_WHITE);
        _display.fillRect(cx - 1, cy - 1, 3, 3, SSD1306_WHITE);

        // Bank pointer at scale top.
        _display.drawTriangle(cx - 3, 10, cx + 3, 10, cx, 14, SSD1306_WHITE);

        // Bottom readouts.
        _display.setCursor(0, 48);
        _display.printf("ROL %+4d", (int)d.roll);
        _display.setCursor(66, 48);
        _display.printf("PIT %+4d", (int)d.pitch);
        _display.setCursor(0, 56);
        _display.printf("ALT %5dm", (int)d.altitude);
        _display.setCursor(66, 56);
        _display.printf("SPD %3d", (int)d.speedKmh);
    }

    // A1: monochrome "jet HUD" - horizon with a gap, ladder rungs with end
    // ticks, boresight ghost and a flight-path vector from vario + speed.
    void drawJetHudMini(const TelemetryData& d) {
        unsigned long now = millis();

        _display.setTextSize(1);
        _display.setCursor(0, 0);
        _display.printf("HDG %03d", ((int)(d.heading + 360.0f)) % 360);
        _display.setCursor(98, 0);
        _display.print(attSourceLabel(d, now));

        const int cx = 64, cy = 29;
        const float kDeg2Rad = 0.0174532925f;
        bool attFresh = d.imuAttLastMs && (now - d.imuAttLastMs) < 2000;
        float roll = (attFresh ? d.roll : 0.0f) * kDeg2Rad;
        float pitch = attFresh ? d.pitch : 0.0f;
        float cr = cosf(roll), sr = sinf(roll);
        const float pxd = 0.95f;

        auto px = [&](float u, float v) -> int { return (int)(cx + u * cr + v * sr); };
        auto py = [&](float u, float v) -> int { return (int)(cy - u * sr + v * cr); };

        // Horizon with a center gap (Gripen style).
        float v0 = pitch * pxd;
        _display.drawLine(px(-64, v0), py(-64, v0), px(-11, v0), py(-11, v0), SSD1306_WHITE);
        _display.drawLine(px(11, v0), py(11, v0), px(64, v0), py(64, v0), SSD1306_WHITE);

        // Ladder rungs every 10 deg, with end ticks toward the horizon.
        for (int deg = -40; deg <= 40; deg += 10) {
            if (deg == 0) continue;
            float v = (pitch - (float)deg) * pxd;
            if (v < -16.0f || v > 16.0f) continue;
            float half = 14.0f;
            float tick = (deg > 0) ? 1.6f : -1.6f;
            _display.drawLine(px(-half, v), py(-half, v), px(half, v), py(half, v),
                              SSD1306_WHITE);
            _display.drawLine(px(-half, v), py(-half, v), px(-half, v + tick),
                              py(-half, v + tick), SSD1306_WHITE);
            _display.drawLine(px(half, v), py(half, v), px(half, v + tick),
                              py(half, v + tick), SSD1306_WHITE);
        }

        // Fixed boresight ghost.
        _display.drawLine(cx - 12, cy, cx - 5, cy, SSD1306_WHITE);
        _display.drawLine(cx - 5, cy, cx - 2, cy + 3, SSD1306_WHITE);
        _display.drawLine(cx + 2, cy + 3, cx + 5, cy, SSD1306_WHITE);
        _display.drawLine(cx + 5, cy, cx + 12, cy, SSD1306_WHITE);

        // Dynamic flight path vector (vario + ground speed).
        if (d.speedKmh > 10.0f) {
            float fpa = atan2f(d.vspeed, d.speedKmh / 3.6f) * (180.0f / 3.14159265f);
            float vf = (pitch - fpa) * pxd;
            if (fabs(vf) < 18.0f) {
                int fx = px(0, vf), fy = py(0, vf);
                _display.drawCircle(fx, fy, 2, SSD1306_WHITE);
                _display.drawFastHLine(fx - 8, fy, 3, SSD1306_WHITE);
                _display.drawFastHLine(fx + 6, fy, 3, SSD1306_WHITE);
            }
        }

        // Bottom corners.
        _display.setCursor(0, 48);
        _display.printf("R %+4d", (int)d.roll);
        _display.setCursor(84, 48);
        _display.print("JET");
        _display.setCursor(0, 56);
        _display.printf("%3dkmh", (int)d.speedKmh);
        _display.setCursor(84, 56);
        _display.printf("%4dm", (int)d.altitude);
    }

    // B: automotive cluster - needle speedo, battery/ESC/power bars.
    void drawSpeedoMini(const TelemetryData& d) {
        const float kDeg2Rad = 0.0174532925f;

        _display.setTextSize(1);
        _display.setCursor(0, 0);
        _display.printf("DRIVE %dS", (int)d.detectedCells);
        _display.setCursor(92, 0);
        _display.printf("HD%03d", ((int)(d.heading + 360.0f)) % 360);

        const int cx = 44, cy = 36, r = 22;
        const int maxSpd = 160;
        for (int s = 0; s <= maxSpd; s += 20) {
            float a = (140.0f + (s / (float)maxSpd) * 260.0f) * kDeg2Rad;
            int x1 = cx + (int)((r - 4) * cosf(a));
            int y1 = cy + (int)((r - 4) * sinf(a));
            int x2 = cx + (int)(r * cosf(a));
            int y2 = cy + (int)(r * sinf(a));
            _display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
        }

        float spd = (d.speedKmh < 0) ? 0 : (d.speedKmh > maxSpd ? maxSpd : d.speedKmh);
        float a = (140.0f + (spd / (float)maxSpd) * 260.0f) * kDeg2Rad;
        _display.drawLine(cx, cy, cx + (int)((r - 6) * cosf(a)),
                          cy + (int)((r - 6) * sinf(a)), SSD1306_WHITE);
        _display.fillCircle(cx, cy, 2, SSD1306_WHITE);

        _display.setTextSize(2);
        _display.setCursor(cx - 17, cy - 4);
        _display.printf("%3d", (int)d.speedKmh);
        _display.setTextSize(1);
        _display.setCursor(cx - 10, cy + 14);
        _display.print("km/h");

        int bx = 76, bw = 46;
        auto bar = [&](int y, float frac, const char* label, const char* value) {
            _display.setCursor(bx, y - 8);
            _display.print(label);
            _display.setCursor(bx + 24, y - 8);
            _display.print(value);
            _display.drawRect(bx, y, bw, 6, SSD1306_WHITE);
            int w = (int)(constrain(frac, 0.0f, 1.0f) * (bw - 4));
            _display.fillRect(bx + 2, y + 2, w, 2, SSD1306_WHITE);
        };

        char buf[12];
        snprintf(buf, sizeof(buf), "%.2fV", d.cellVoltage);
        bar(18, (d.cellVoltage - 3.0f) / 1.25f, "BAT", buf);
        snprintf(buf, sizeof(buf), "%dC", (int)d.escTemp);
        bar(36, (d.escTemp - 20.0f) / 80.0f, "ESC", buf);
        snprintf(buf, sizeof(buf), "%dW", (int)d.powerWatts);
        bar(54, d.powerWatts / 1200.0f, "PWR", buf);
    }

    // C: marine console - compass rose, stern heel view, side pitch view.
    void drawMarineMini(const TelemetryData& d) {
        const float kDeg2Rad = 0.0174532925f;
        const int ccx = 34, ccy = 32, cr = 23;
        float hdg = d.heading;
        float roll = d.roll;
        float pitch = d.pitch;

        // Compass rose ticks every 30 deg.
        for (int deg = 0; deg < 360; deg += 30) {
            float ang = (deg - hdg) * kDeg2Rad;
            int x1 = ccx + (int)((cr - 3) * sinf(ang));
            int y1 = ccy - (int)((cr - 3) * cosf(ang));
            int x2 = ccx + (int)(cr * sinf(ang));
            int y2 = ccy - (int)(cr * cosf(ang));
            _display.drawLine(x1, y1, x2, y2, SSD1306_WHITE);
        }
        _display.drawCircle(ccx, ccy, cr, SSD1306_WHITE);

        const char* card[4] = {"N", "E", "S", "W"};
        for (int i = 0; i < 4; i++) {
            float ang = ((i * 90) - hdg) * kDeg2Rad;
            int lx = ccx + (int)((cr - 8) * sinf(ang)) - 2;
            int ly = ccy - (int)((cr - 8) * cosf(ang)) - 3;
            _display.setCursor(lx, ly);
            _display.print(card[i]);
        }
        // Fixed bow marker.
        _display.drawTriangle(ccx - 3, ccy - cr - 6, ccx + 3, ccy - cr - 6,
                              ccx, ccy - cr - 2, SSD1306_WHITE);
        // Digital heading in the rose center.
        _display.setTextSize(2);
        _display.setCursor(ccx - 17, ccy - 6);
        _display.printf("%03d", ((int)(hdg + 360.0f)) % 360);
        _display.setTextSize(1);

        // Rotated segment helper (local coords around a center).
        auto seg = [&](float x1, float y1, float x2, float y2, float ang, int cx,
                       int cy) {
            float c = cosf(ang), s = sinf(ang);
            int ax = cx + (int)(x1 * c - y1 * s);
            int ay = cy + (int)(x1 * s + y1 * c);
            int bx = cx + (int)(x2 * c - y2 * s);
            int by = cy + (int)(x2 * s + y2 * c);
            _display.drawLine(ax, ay, bx, by, SSD1306_WHITE);
        };

        // Right column: heel (stern view).
        _display.setCursor(66, 0);
        _display.printf("HEEL %+4d", (int)roll);
        _display.drawFastHLine(68, 22, 58, SSD1306_WHITE);  // waterline
        float heel = roll * kDeg2Rad;
        seg(-16, -2, 16, -2, heel, 97, 18);   // hull bottom
        seg(16, -2, 12, 5, heel, 97, 18);
        seg(12, 5, -12, 5, heel, 97, 18);
        seg(-12, 5, -16, -2, heel, 97, 18);
        seg(-5, -8, 5, -8, heel, 97, 18);     // superstructure
        seg(5, -8, 5, -2, heel, 97, 18);
        seg(-5, -8, -5, -2, heel, 97, 18);

        // Right column: pitch (side view on a tilted waterline).
        _display.setCursor(66, 30);
        _display.printf("PITCH %+4d", (int)pitch);
        float tilt = pitch * kDeg2Rad;
        seg(-30, 0, 30, 0, tilt, 97, 50);     // waterline
        seg(-14, -2, 14, -2, tilt, 97, 50);   // hull
        seg(14, -2, 10, 4, tilt, 97, 50);
        seg(10, 4, -10, 4, tilt, 97, 50);
        seg(-10, 4, -14, -2, tilt, 97, 50);
        seg(-3, -9, 3, -9, tilt, 97, 50);     // mast
        seg(0, -9, 0, -2, tilt, 97, 50);

        // Bottom row readouts.
        _display.setCursor(0, 56);
        _display.printf("%dkmh", (int)d.speedKmh);
        _display.setCursor(40, 56);
        _display.printf("%dkn", (int)(d.speedKmh / 1.852f));
        _display.setCursor(84, 56);
        _display.printf("%4dm", (int)d.altitude);
    }

public:
    OledRenderer() : _display(128, 64, &Wire, -1) {}

    // Raw 1 KB SSD1306 framebuffer (for the pixel-exact web mirror).
    const uint8_t* buffer() { return _display.getBuffer(); }

    bool begin(int sdaPin, int sclPin) {
        Wire.begin(sdaPin, sclPin);
        Wire.setClock(400000);  // explicit fast-mode I2C for full-frame updates
        if (!_display.begin(SSD1306_SWITCHCAPVCC, 0x3C)) return false;
        _display.setTextColor(SSD1306_WHITE);
        _display.clearDisplay();
        _display.display();
        return true;
    }

    void render(const TelemetryData& data, const SystemConfig& cfg, const char* protoName) {
        _display.setRotation(cfg.flipOled ? 2 : 0);
        _display.clearDisplay();

        switch (cfg.oledPreset) {
            case OledPreset::TELEMETRY_INFO: drawInfoPreset(data, cfg); break;
            case OledPreset::EDF_DRIVETRAIN: drawEdfPreset(data); break;
            case OledPreset::SENSOR_DEBUG:   drawDebugPreset(data, protoName); break;
            case OledPreset::ATTITUDE_HUD:   drawAttitudePreset(data, cfg.webTheme); break;
        }

        _display.display();
    }

    // Diagnostic patterns to prove out the panel itself.
    void renderTestPattern(uint8_t idx) {
        _display.clearDisplay();
        switch (idx % 6) {
            case 0:
                _display.fillScreen(SSD1306_WHITE);
                break;
            case 1:
                break;  // all black
            case 2:
                for (int y = 0; y < 64; y += 8)
                    for (int x = 0; x < 128; x += 8)
                        if (((x / 8) + (y / 8)) & 1) _display.fillRect(x, y, 8, 8, SSD1306_WHITE);
                break;
            case 3:
                for (int y = 0; y < 64; y += 2)
                    _display.drawFastHLine(0, y, 128, SSD1306_WHITE);
                break;
            case 4:
                for (int x = 0; x < 128; x += 2)
                    _display.drawFastVLine(x, 0, 64, SSD1306_WHITE);
                break;
            default:
                _display.drawRect(0, 0, 128, 64, SSD1306_WHITE);
                _display.drawRect(12, 12, 104, 40, SSD1306_WHITE);
                _display.drawLine(0, 0, 127, 63, SSD1306_WHITE);
                _display.drawLine(127, 0, 0, 63, SSD1306_WHITE);
                break;
        }
        _display.display();
    }
};
