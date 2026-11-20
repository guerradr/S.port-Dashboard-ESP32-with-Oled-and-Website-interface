#pragma once
#include "ProtocolParser.h"

class MspParser : public ProtocolParser {
public:
    const char* name() const override { return "MSP (BF/INAV)"; }
    void begin(HardwareSerial& serial, int rxPin, int txPin) override {
        serial.begin(115200, SERIAL_8N1, rxPin, txPin);
        serial.setRxInvert(false);
        serial.setTxInvert(false);
    }
    void update(TelemetryData& data) override {
        (void)data;
        // Phase 2 hook: Parse MSP v1/v2 frames (ATTITUDE, ALTITUDE, ANALOG, RAW_GPS)
    }
};

class MavlinkParser : public ProtocolParser {
public:
    const char* name() const override { return "MAVLink"; }
    void begin(HardwareSerial& serial, int rxPin, int txPin) override {
        serial.begin(57600, SERIAL_8N1, rxPin, txPin);
        serial.setRxInvert(false);
        serial.setTxInvert(false);
    }
    void update(TelemetryData& data) override {
        (void)data;
        // Phase 2 hook: Parse MAVLink v1/v2 packets (SYS_STATUS, ATTITUDE, VFR_HUD)
    }
};

class CrsfParser : public ProtocolParser {
public:
    const char* name() const override { return "CRSF (ELRS)"; }
    void begin(HardwareSerial& serial, int rxPin, int txPin) override {
        serial.begin(420000, SERIAL_8N1, rxPin, txPin);
        serial.setRxInvert(false);
        serial.setTxInvert(false);
    }
    void update(TelemetryData& data) override {
        (void)data;
        // Phase 3 hook: Parse CRSF frames (BATTERY_SENSOR, LINK_STATISTICS, ATTITUDE)
    }
};

class JetiParser : public ProtocolParser {
public:
    const char* name() const override { return "Jeti EX"; }
    void begin(HardwareSerial& serial, int rxPin, int txPin) override {
        serial.begin(125000, SERIAL_8N1, rxPin, txPin);
        serial.setRxInvert(false);
        serial.setTxInvert(false);
    }
    void update(TelemetryData& data) override {
        (void)data;
        // Phase 3 hook: Parse Jeti EX telemetry packets
    }
};
