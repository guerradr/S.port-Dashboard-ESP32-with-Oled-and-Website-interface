#pragma once
#include <Arduino.h>

// ---------------------------------------------------------------------------
// System-wide enums and data model for the telemetry multitool (v2.0.0)
// ---------------------------------------------------------------------------

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
    SENSOR_DEBUG   = 2,
    ATTITUDE_HUD   = 3
};
static constexpr uint8_t OLED_PRESET_COUNT = 4;

// What each openXsensor ACC slot (0x0700 / 0x0710 / 0x0720) carries.
enum class AttSlot : uint8_t {
    NONE  = 0,
    PITCH = 1,
    ROLL  = 2,
    YAW   = 3
};

// S.Port bus role.  AUTO promotes the ESP32 to bus master when the bus is
// silent for 2 s (no receiver on the bench), and demotes as soon as a real
// receiver is detected polling again.
enum class BusMode : uint8_t {
    AUTO = 0,
    RX_ONLY = 1,
    BENCH = 2
};

// openXsensor can be configured to send GPS speed in knots (default) or km/h.
enum class GpsSpeedUnit : uint8_t {
    KNOTS = 0,
    KMH = 1
};

static constexpr uint8_t MAX_CELLS = 6;

struct TelemetryData {
    // -- Power & drivetrain ---------------------------------------------------
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

    // -- FLVSS cell voltages (S.Port 0x0300) ----------------------------------
    float cellV[MAX_CELLS] = {0, 0, 0, 0, 0, 0};
    uint8_t cellCount = 0;
    float minCellV = 0.0f;
    bool cellsValid = false;
    unsigned long cellsLastMs = 0;

    // -- Flight & navigation ---------------------------------------------------
    float altitude = 0.0f;
    float maxAltitude = 0.0f;
    float vspeed = 0.0f;
    float speedKmh = 0.0f;
    float speedRaw = 0.0f;   // raw GPS speed value (unit depends on oXs config)
    float maxSpeedKmh = 0.0f;
    float roll = 0.0f;
    float pitch = 0.0f;
    float heading = 0.0f;

    float gpsLat = 0.0f;
    float gpsLon = 0.0f;
    float gpsAlt = 0.0f;
    bool gpsPosValid = false;
    unsigned long gpsPosLastMs = 0;
    uint8_t gpsSats = 0;
    int rssi = 0;

    // -- openXsensor ACC slots (raw int32, 0.1 degree units when mapped) ------
    int32_t accRaw[3] = {0, 0, 0};
    unsigned long accLastMs[3] = {0, 0, 0};

    // -- Attitude / heading freshness -----------------------------------------
    unsigned long imuAttLastMs = 0;   // last IMU pitch/roll update
    unsigned long hdgGpsMs = 0;       // last GPS course update
    unsigned long hdgImuMs = 0;       // last IMU yaw heading update

    // -- Bus & sensor diagnostics ---------------------------------------------
    uint32_t validPackets = 0;
    uint32_t crcErrors = 0;
    uint16_t packetsPerSec = 0;
    uint8_t lastPhysId = 0;
    uint16_t lastAppId = 0;
    uint32_t lastRawValue = 0;
    uint16_t activeSensorMask = 0;
    unsigned long lastPacketMs = 0;
    unsigned long lastByteMs = 0;
    uint32_t framesDecoded = 0;

    // -- Bus master diagnostics ------------------------------------------------
    bool busMasterActive = false;
    bool busRxSeen = false;
    uint32_t pollsSent = 0;

    // ------------------------------------------------------------------------
    bool cellsFresh(unsigned long now) const {
        return cellsValid && cellsLastMs && (now - cellsLastMs < 5000);
    }

    void updateDerived(float dtSeconds) {
        unsigned long now = millis();
        totalVoltage = vfas1 + vfas2;

        bool fresh = cellsFresh(now);
        if (fresh) {
            float mn = 99.0f;
            for (uint8_t i = 0; i < MAX_CELLS; i++) {
                if (cellV[i] > 0.3f && cellV[i] < mn) mn = cellV[i];
            }
            minCellV = (mn < 99.0f) ? mn : 0.0f;
            if (totalVoltage > 2.5f) {
                if (totalVoltage < minVoltage) minVoltage = totalVoltage;
                cellVoltage = (minCellV > 0.0f)
                                  ? minCellV
                                  : totalVoltage / (float)max((uint8_t)1, detectedCells);
            }
        } else if (totalVoltage > 2.5f) {
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

        gpsPosValid = gpsPosLastMs && (now - gpsPosLastMs < 10000);
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

    // openXsensor ACC slot mapping (ACCX, ACCY, ACCZ) and axis signs.
    uint8_t attMap[3] = {(uint8_t)AttSlot::PITCH, (uint8_t)AttSlot::ROLL,
                         (uint8_t)AttSlot::YAW};
    bool invPitch = false;
    bool invRoll = false;
    bool invYaw = false;

    uint8_t busMode = (uint8_t)BusMode::AUTO;
    uint8_t gpsUnit = (uint8_t)GpsSpeedUnit::KNOTS;
    uint8_t attSmooth = 70;  // 0 = raw, 100 = heavily smoothed
};

// Human readable attitude source for the OLED / web badge.
inline const char* attSourceLabel(const TelemetryData& d, unsigned long now) {
    if (d.imuAttLastMs && (now - d.imuAttLastMs) < 2000) return "OXS";
    if (d.hdgGpsMs && (now - d.hdgGpsMs) < 10000) return "GPS";
    return "NONE";
}

inline const char* headingSourceLabel(const TelemetryData& d, unsigned long now) {
    if (d.hdgGpsMs && (now - d.hdgGpsMs) < 10000) return "GPS";
    if (d.hdgImuMs && (now - d.hdgImuMs) < 5000) return "IMU";
    return "NONE";
}
