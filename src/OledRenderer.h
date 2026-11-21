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
        _display.printf("EDF %dS   ESC %dC", (int)d.detectedCells, (int)d.escTemp);

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
        _display.printf("PK %dW  MAH %dmAh", (int)d.peakWatts, (int)d.mahUsed);

        _display.drawFastHLine(0, 40, 128, SSD1306_WHITE);

        // ESC temperature bar (20..100 C)
        _display.setCursor(0, 42);
        _display.print("ESC");
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
    // Preset 4: Mini attitude HUD (pitch ladder, horizon, heading).
    // ------------------------------------------------------------------
    void drawAttitudePreset(const TelemetryData& d) {
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

        auto px = [&](float u, float v) -> int { return (int)(cx + u * cr - v * sr); };
        auto py = [&](float u, float v) -> int { return (int)(cy + u * sr + v * cr); };

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

public:
    OledRenderer() : _display(128, 64, &Wire, -1) {}

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
            case OledPreset::ATTITUDE_HUD:   drawAttitudePreset(data); break;
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
