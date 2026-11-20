#pragma once
#include <Adafruit_GFX.h>
#include <Adafruit_SSD1306.h>
#include "TelemetryBus.h"

class OledRenderer {
private:
    Adafruit_SSD1306 _display;

    String formatMetric(const String& key, const TelemetryData& d) {
        if (key == "VOLT") return String(d.totalVoltage, 1) + "V";
        if (key == "CELL") return String(d.cellVoltage, 2) + "V";
        if (key == "CURR") return String(d.current, 1) + "A";
        if (key == "PWR")  return String((int)d.powerWatts) + "W";
        if (key == "MAH")  return String((int)d.mahUsed) + "mAh";
        if (key == "RPM")  return String((int)d.rpm);
        if (key == "TEMP") return String((int)d.escTemp) + "C";
        if (key == "ALT")  return String((int)d.altitude) + "m";
        if (key == "SPD")  return String((int)d.speedKmh) + "kmh";
        if (key == "RSSI") return String(d.rssi) + "dB";
        return "--";
    }

    void drawInfoPreset(const TelemetryData& d, const SystemConfig& cfg) {
        _display.setTextSize(1);
        _display.setCursor(0, 0);
        _display.printf("%.2fV (%.2fV)", d.totalVoltage, d.cellVoltage);
        _display.setCursor(86, 0);
        _display.printf("RS:%d", d.rssi);
        _display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

        for (int i = 0; i < 4; i++) {
            int col = i % 2;
            int row = i / 2;
            int x = col * 64 + 2;
            int y = 14 + row * 25;
            _display.setCursor(x, y);
            _display.print(cfg.customOledSlots[i]);
            _display.setCursor(x, y + 10);
            _display.print(formatMetric(cfg.customOledSlots[i], d));
        }
    }

    void drawEdfPreset(const TelemetryData& d) {
        _display.setTextSize(1);
        _display.setCursor(0, 0);
        _display.printf("EDF BENCH   %dS LiPo", d.detectedCells);
        _display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

        _display.setCursor(0, 14);
        _display.printf("PWR: %4dW  CUR:%4.1fA", (int)d.powerWatts, d.current);
        _display.setCursor(0, 26);
        _display.printf("VOL: %5.2fV RPM:%5d", d.totalVoltage, (int)d.rpm);
        _display.setCursor(0, 38);
        _display.printf("MIN: %5.2fV PK:%5dW", d.minVoltage, (int)d.peakWatts);

        _display.setCursor(0, 52);
        _display.printf("ESC %2dC", (int)d.escTemp);
        _display.drawRect(48, 51, 78, 9, SSD1306_WHITE);
        int w = constrain(map((int)d.escTemp, 20, 100, 0, 74), 0, 74);
        _display.fillRect(50, 53, w, 5, SSD1306_WHITE);
    }

    void drawDebugPreset(const TelemetryData& d, const char* protoName) {
        _display.setTextSize(1);
        _display.setCursor(0, 0);
        _display.print(protoName);
        _display.drawLine(0, 10, 127, 10, SSD1306_WHITE);

        _display.setCursor(0, 14);
        _display.printf("FPS: %3d   CRC:%lu", d.packetsPerSec, d.crcErrors);
        _display.setCursor(0, 26);
        _display.printf("APP: 0x%04X PHY:0x%02X", d.lastAppId, d.lastPhysId);
        _display.setCursor(0, 38);
        _display.printf("PKT: %lu", d.validPackets);

        _display.setCursor(0, 52);
        _display.print("ACT:");
        if (d.activeSensorMask & (1 << 0)) _display.print(" V");
        if (d.activeSensorMask & (1 << 1)) _display.print(" I");
        if (d.activeSensorMask & (1 << 2)) _display.print(" RPM");
        if (d.activeSensorMask & (1 << 3)) _display.print(" TMP");
        if (d.activeSensorMask & (1 << 5)) _display.print(" ALT");
        if (d.activeSensorMask & (1 << 7)) _display.print(" GPS");
    }

public:
    OledRenderer() : _display(128, 64, &Wire, -1) {}

    bool begin(int sdaPin, int sclPin) {
        Wire.begin(sdaPin, sclPin);
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
        }

        _display.display();
    }
};
