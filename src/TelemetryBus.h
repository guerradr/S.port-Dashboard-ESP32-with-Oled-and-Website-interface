#pragma once
#include <Arduino.h>

enum class ActiveProtocol : uint8_t {
    FRSKY_SPORT = 0,
    MSP_FC      = 1,
    MAVLINK     = 2,
    CRSF        = 3,
    JETI_EX     = 4
};

enum class OledPreset : uint8_t {
    TELEMETRY_INFO = 0,
    EDF_DRIVETRAIN = 1,
    SENSOR_DEBUG   = 2
};

struct TelemetryData {
    // Power & Drivetrain
    float vfas1 = 0.0f;
    float vfas2 = 0.0f;
    float totalVoltage = 0.0f;
    float cellVoltage = 0.0f;
    uint8_t detectedCells = 4;
    float minVoltage = 99.9f;

    float current = 0.0f;
    float peakCurrent = 0.0f;
    float powerWatts = 0.0f;
    float peakWatts = 0.0f;
    float mahUsed = 0.0f;
    float rpm = 0.0f;
    float escTemp = 0.0f;
    float motorTemp = 0.0f;

    // Flight & Navigation
    float altitude = 0.0f;
    float maxAltitude = 0.0f;
    float vspeed = 0.0f;
    float speedKmh = 0.0f;
    float maxSpeedKmh = 0.0f;
    float roll = 0.0f;
    float pitch = 0.0f;
    float heading = 0.0f;
    int rssi = 0;
    uint8_t gpsSats = 0;

    // Bus & Sensor Diagnostics
    uint32_t validPackets = 0;
    uint32_t crcErrors = 0;
    uint16_t packetsPerSec = 0;
    uint8_t lastPhysId = 0;
    uint16_t lastAppId = 0;
    uint32_t lastRawValue = 0;
    uint16_t activeSensorMask = 0;
    unsigned long lastPacketMs = 0;

    void updateDerived(float dtSeconds) {
        totalVoltage = vfas1 + vfas2;
        if (totalVoltage > 2.5f) {
            if (totalVoltage < minVoltage) minVoltage = totalVoltage;
            cellVoltage = totalVoltage / (float)max((uint8_t)1, detectedCells);
        }
        powerWatts = totalVoltage * current;
        if (current > peakCurrent) peakCurrent = current;
        if (powerWatts > peakWatts) peakWatts = powerWatts;
        if (dtSeconds > 0.0f && current > 0.05f) {
            mahUsed += (current * 1000.0f) * (dtSeconds / 3600.0f);
        }
        if (altitude > maxAltitude) maxAltitude = altitude;
        if (speedKmh > maxSpeedKmh) maxSpeedKmh = speedKmh;
    }

    void resetPeaks() {
        minVoltage = (totalVoltage > 2.5f) ? totalVoltage : 99.9f;
        peakCurrent = 0.0f;
        peakWatts = 0.0f;
        mahUsed = 0.0f;
        maxAltitude = 0.0f;
        maxSpeedKmh = 0.0f;
    }
};

struct SystemConfig {
    ActiveProtocol protocol = ActiveProtocol::FRSKY_SPORT;
    OledPreset oledPreset = OledPreset::TELEMETRY_INFO;
    uint8_t manualCells = 0;
    bool flipOled = false;
    String webTheme = "A1";
    String customOledSlots[4] = {"VOLT", "CURR", "ALT", "SPD"};
    String customWebLayoutJson = "";
};
