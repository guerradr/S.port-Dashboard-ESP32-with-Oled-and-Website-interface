#pragma once
#include "ProtocolParser.h"
#include <freertos/FreeRTOS.h>
#include <freertos/task.h>

// ---------------------------------------------------------------------------
// FrSky S.Port parser (openXsensor compatible)
//
// Decodes:
//   0x0100 ALT, 0x0110 VARIO, 0x0200 CURR, 0x0210/0x0211 VFAS,
//   0x0300 FLVSS cells, 0x0400/0x0410 temps, 0x0500 RPM,
//   0x0700/0x0710/0x0720 openXsensor ACC slots (PITCH/ROLL/YAW, 0.1 deg),
//   0x0800/0x0820 GPS lat/lon, 0x0830 speed, 0x0840 course, 0x0850 altitude,
//   0xF101 receiver RSSI.
//
// Bench master mode (v2.1): the ESP32 polls the bus from a dedicated FreeRTOS
// task (10 ms cadence, immune to OLED/WebSocket stalls) with an adaptive
// rotation that fast-polls sensors that recently answered and only probes the
// remaining physical IDs for discovery.
// ---------------------------------------------------------------------------

static constexpr uint8_t SPORT_START_STOP = 0x7E;
static constexpr uint8_t SPORT_BYTESTUFF  = 0x7D;
static constexpr uint8_t SPORT_STUFF_MASK = 0x20;
static constexpr uint8_t SPORT_DATA_FRAME = 0x10;

static constexpr uint16_t APPID_ALT_FIRST   = 0x0100;
static constexpr uint16_t APPID_VARIO_FIRST = 0x0110;
static constexpr uint16_t APPID_CURR_FIRST  = 0x0200;
static constexpr uint16_t APPID_VFAS_FIRST  = 0x0210;
static constexpr uint16_t APPID_VFAS_SECOND = 0x0211;
static constexpr uint16_t APPID_CELLS_FIRST = 0x0300;
static constexpr uint16_t APPID_TEMP1_FIRST = 0x0400;
static constexpr uint16_t APPID_TEMP2_FIRST = 0x0410;
static constexpr uint16_t APPID_RPM_FIRST   = 0x0500;
static constexpr uint16_t APPID_ACCX        = 0x0700;  // openXsensor ACC slot X
static constexpr uint16_t APPID_ACCY        = 0x0710;  // openXsensor ACC slot Y
static constexpr uint16_t APPID_ACCZ        = 0x0720;  // openXsensor ACC slot Z
static constexpr uint16_t APPID_GPS_LAT     = 0x0800;
static constexpr uint16_t APPID_GPS_LON     = 0x0820;
static constexpr uint16_t APPID_GPS_SPEED   = 0x0830;
static constexpr uint16_t APPID_GPS_COURSE  = 0x0840;
static constexpr uint16_t APPID_GPS_ALT     = 0x0850;
static constexpr uint16_t APPID_RSSI        = 0xF101;

// Physical sensor IDs polled by FrSky receivers (source: openXsensor docs).
static const uint8_t SPORT_POLL_IDS[] = {
    0x00, 0xA1, 0x22, 0x83, 0xE4, 0x45, 0xC6, 0x67, 0x48, 0xE9, 0x6A,
    0xCB, 0xAC, 0x0D, 0x8E, 0x2F, 0xD0, 0x71, 0xF2, 0x53, 0x34, 0x95,
    0x16, 0xB7, 0x98, 0x39, 0xBA, 0x1B};
static constexpr uint8_t SPORT_POLL_ID_COUNT =
    sizeof(SPORT_POLL_IDS) / sizeof(SPORT_POLL_IDS[0]);

static constexpr unsigned long SPORT_POLL_SPACING_MS   = 10;
static constexpr unsigned long SPORT_AUTO_SILENCE_MS   = 2000;
static constexpr unsigned long SPORT_AUTO_CYCLE_MS     = 2000;
static constexpr unsigned long SPORT_AUTO_LISTEN_MS    = 100;
static constexpr unsigned long SPORT_ACTIVE_WINDOW_MS  = 5000;
static constexpr uint16_t SPORT_DISCOVERY_EVERY        = 100;  // ~1 s at 10 ms
static constexpr size_t SPORT_RX_BUFFER                = 512;

class SportParser : public ProtocolParser {
private:
    HardwareSerial* _port = nullptr;
    uint8_t _buf[9];
    uint8_t _idx = 0;
    bool _inFrame = false;
    bool _stuffed = false;
    uint32_t _fpsCounter = 0;
    unsigned long _lastFpsTime = 0;

    // ---- Bus master state (shared with the poll task) --------------------
    volatile BusMode _busMode = BusMode::AUTO;
    volatile bool _masterActive = false;
    volatile bool _pollEnabled = false;
    volatile uint32_t _pollsSent = 0;
    volatile unsigned long _idLastSeen[SPORT_POLL_ID_COUNT] = {};
    volatile unsigned long _lastRxByteMs = 0;
    unsigned long _masterRunStart = 0;
    unsigned long _listenStart = 0;
    bool _inListenWindow = false;
    bool _taskStarted = false;
    uint16_t _pollCounter = 0;
    uint8_t _activeIdx = 0;
    uint8_t _discoveryIdx = 0;

    bool checkCrc(const uint8_t* packet) {
        uint16_t crc = 0;
        for (int i = 1; i <= 7; i++) {
            crc += packet[i];
            crc += crc >> 8;
            crc &= 0x00FF;
        }
        return ((0xFF - crc) == packet[8]);
    }

    void notePhysId(uint8_t id) {
        for (uint8_t i = 0; i < SPORT_POLL_ID_COUNT; i++) {
            if (SPORT_POLL_IDS[i] == id) {
                _idLastSeen[i] = millis();
                return;
            }
        }
    }

    bool idActive(uint8_t idx, unsigned long now) const {
        return _idLastSeen[idx] && (now - _idLastSeen[idx]) < SPORT_ACTIVE_WINDOW_MS;
    }

    void decodePayload(uint8_t physId, uint16_t appId, int32_t val, TelemetryData& d) {
        d.lastPhysId = physId;
        d.lastAppId = appId;
        d.lastRawValue = (uint32_t)val;
        d.lastPacketMs = millis();
        d.framesDecoded++;

        uint16_t baseId = appId & 0xFFF0;

        if (appId == APPID_VFAS_SECOND) {
            d.vfas2 = val / 100.0f;
            d.activeSensorMask |= (1 << 0);
        } else if (baseId == APPID_VFAS_FIRST) {
            d.vfas1 = val / 100.0f;
            d.activeSensorMask |= (1 << 0);
        } else if (baseId == APPID_CURR_FIRST) {
            d.current = val / 10.0f;
            d.activeSensorMask |= (1 << 1);
        } else if (baseId == APPID_RPM_FIRST) {
            d.rpm = (float)val;
            d.activeSensorMask |= (1 << 2);
        } else if (baseId == APPID_TEMP1_FIRST) {
            d.escTemp = (float)val;
            d.activeSensorMask |= (1 << 3);
        } else if (baseId == APPID_TEMP2_FIRST) {
            d.motorTemp = (float)val;
            d.activeSensorMask |= (1 << 4);
        } else if (baseId == APPID_ALT_FIRST) {
            d.altitude = val / 100.0f;
            d.activeSensorMask |= (1 << 5);
        } else if (baseId == APPID_VARIO_FIRST) {
            d.vspeed = val / 100.0f;
            d.activeSensorMask |= (1 << 6);
        } else if (baseId == APPID_CELLS_FIRST) {
            // FLVSS frame: bits 0-3 = pair index, 4-7 = cell count,
            // bits 8-19 = cell n (mV/2), bits 20-31 = cell n+1 (mV/2).
            uint32_t raw = (uint32_t)val;
            uint8_t id = raw & 0x0F;
            uint8_t count = (raw >> 4) & 0x0F;
            float c1 = ((raw >> 8) & 0x0FFF) * 0.002f;
            float c2 = ((raw >> 20) & 0x0FFF) * 0.002f;
            if (id < MAX_CELLS) d.cellV[id] = c1;
            if (id + 1 < MAX_CELLS) d.cellV[id + 1] = c2;
            if (count > 0 && count <= MAX_CELLS) d.cellCount = count;
            d.cellsValid = true;
            d.cellsLastMs = millis();
            d.activeSensorMask |= (1 << 10);
        } else if (appId == APPID_ACCX) {
            d.accRaw[0] = val;
            d.accLastMs[0] = millis();
            d.activeSensorMask |= (1 << 9);
        } else if (appId == APPID_ACCY) {
            d.accRaw[1] = val;
            d.accLastMs[1] = millis();
            d.activeSensorMask |= (1 << 9);
        } else if (appId == APPID_ACCZ) {
            d.accRaw[2] = val;
            d.accLastMs[2] = millis();
            d.activeSensorMask |= (1 << 9);
        } else if (baseId == APPID_GPS_LAT) {
            uint32_t raw = (uint32_t)val;
            float lat = (float)(raw & 0x3FFFFFFF) / 600000.0f;
            if (raw & 0x40000000) lat = -lat;
            d.gpsLat = lat;
            d.gpsPosLastMs = millis();
            d.activeSensorMask |= (1 << 11);
        } else if (baseId == APPID_GPS_LON) {
            uint32_t raw = (uint32_t)val;
            float lon = (float)(raw & 0x3FFFFFFF) / 600000.0f;
            if (raw & 0x40000000) lon = -lon;
            d.gpsLon = lon;
            d.gpsPosLastMs = millis();
            d.activeSensorMask |= (1 << 11);
        } else if (baseId == APPID_GPS_SPEED) {
            d.speedRaw = (float)(uint32_t)val;
            d.activeSensorMask |= (1 << 7);
        } else if (baseId == APPID_GPS_COURSE) {
            d.heading = val / 100.0f;
            d.hdgGpsMs = millis();
            d.activeSensorMask |= (1 << 7);
        } else if (baseId == APPID_GPS_ALT) {
            d.gpsAlt = val / 100.0f;
            d.activeSensorMask |= (1 << 11);
        } else if (appId == APPID_RSSI) {
            d.rssi = (int)(val & 0xFF);
            d.activeSensorMask |= (1 << 8);
        }
    }

    void feedByte(uint8_t b, TelemetryData& data) {
        if (b == SPORT_START_STOP) {
            _idx = 0;
            _inFrame = true;
            _stuffed = false;
            return;
        }
        if (!_inFrame) return;

        if (b == SPORT_BYTESTUFF) {
            _stuffed = true;
            return;
        }
        if (_stuffed) {
            b ^= SPORT_STUFF_MASK;
            _stuffed = false;
        }

        if (_idx < 9) _buf[_idx++] = b;

        if (_idx == 9) {
            _inFrame = false;
            if (_buf[1] == SPORT_DATA_FRAME) {
                if (checkCrc(_buf)) {
                    uint16_t appId = (uint16_t)_buf[2] | ((uint16_t)_buf[3] << 8);
                    int32_t value = (int32_t)((uint32_t)_buf[4] |
                                              ((uint32_t)_buf[5] << 8) |
                                              ((uint32_t)_buf[6] << 16) |
                                              ((uint32_t)_buf[7] << 24));
                    notePhysId(_buf[0]);
                    decodePayload(_buf[0], appId, value, data);
                    data.validPackets++;
                    _fpsCounter++;
                } else {
                    data.crcErrors++;
                }
            }
        }
    }

    // ---- Poll scheduling (runs in its own FreeRTOS task) ------------------
    void sendNextPoll() {
        if (!_port) return;
        unsigned long now = millis();

        uint8_t active = 0;
        for (uint8_t i = 0; i < SPORT_POLL_ID_COUNT; i++) {
            if (idActive(i, now)) active++;
        }

        uint8_t id;
        if (active == 0) {
            // Nothing known on the bus: full discovery rotation.
            id = SPORT_POLL_IDS[_discoveryIdx % SPORT_POLL_ID_COUNT];
            _discoveryIdx++;
        } else if ((_pollCounter++ % SPORT_DISCOVERY_EVERY) == 0) {
            // Periodic single probe outside the known responders.
            id = SPORT_POLL_IDS[_discoveryIdx % SPORT_POLL_ID_COUNT];
            _discoveryIdx++;
        } else {
            // Fast rotation over the sensors that actually answer.
            uint8_t guard = 0;
            uint8_t cand = 0;
            do {
                cand = _activeIdx % SPORT_POLL_ID_COUNT;
                _activeIdx++;
            } while (!idActive(cand, now) && ++guard <= SPORT_POLL_ID_COUNT);
            id = SPORT_POLL_IDS[cand];
        }

        _port->write(SPORT_START_STOP);
        _port->write(id);
        _pollsSent++;
    }

    static void pollTaskThunk(void* arg) {
        static_cast<SportParser*>(arg)->pollTaskLoop();
    }

    void pollTaskLoop() {
        for (;;) {
            vTaskDelay(pdMS_TO_TICKS(SPORT_POLL_SPACING_MS));
            if (_masterActive && _pollEnabled) sendNextPoll();
        }
    }

    void serviceMaster(unsigned long now, TelemetryData& d) {
        if (_busMode == BusMode::RX_ONLY) {
            _masterActive = false;
            _pollEnabled = false;
            _inListenWindow = false;
            d.busMasterActive = false;
            return;
        }

        if (_busMode == BusMode::AUTO) {
            if (!_masterActive &&
                (now - _lastRxByteMs) < SPORT_AUTO_SILENCE_MS) {
                _pollEnabled = false;
                d.busMasterActive = false;
                return;
            }
        }

        if (!_masterActive) {
            _masterActive = true;
            _masterRunStart = now;
            _listenStart = now;
            _inListenWindow = false;
            _pollCounter = 0;
            _activeIdx = 0;
            _discoveryIdx = 0;
        }
        d.busMasterActive = true;

        if (_busMode == BusMode::AUTO) {
            unsigned long cyclePos = (now - _masterRunStart) % SPORT_AUTO_CYCLE_MS;
            bool listenPhase =
                cyclePos >= (SPORT_AUTO_CYCLE_MS - SPORT_AUTO_LISTEN_MS);
            if (listenPhase) {
                if (!_inListenWindow) {
                    _inListenWindow = true;
                    _listenStart = now;
                }
                _pollEnabled = false;
                // A real receiver polls continuously; hearing anything while
                // we are not transmitting means someone else owns the bus.
                if (_lastRxByteMs > (_listenStart + 5)) {
                    _masterActive = false;
                    _pollEnabled = false;
                    _inListenWindow = false;
                    d.busMasterActive = false;
                    _lastRxByteMs = now;
                    return;
                }
                return;
            }
            _inListenWindow = false;
        }

        _pollEnabled = true;
    }

public:
    const char* name() const override { return "FrSky S.Port"; }

    void setBusMode(BusMode mode) override {
        _busMode = mode;
        if (mode == BusMode::RX_ONLY) {
            _masterActive = false;
            _pollEnabled = false;
            _inListenWindow = false;
        }
    }

    BusMode busMode() const { return _busMode; }
    bool masterActive() const { return _masterActive; }

    void begin(HardwareSerial& serial, int rxPin, int txPin) override {
        _port = &serial;
        _port->setRxBufferSize(SPORT_RX_BUFFER);
        _port->begin(57600, SERIAL_8N1, rxPin, txPin);
        _port->setRxInvert(true);
        _port->setTxInvert(true);
        _idx = 0;
        _inFrame = false;
        _stuffed = false;
        _lastFpsTime = millis();
        _lastRxByteMs = millis();
        _masterActive = false;
        _pollEnabled = false;
        _inListenWindow = false;
        _pollsSent = 0;
        for (uint8_t i = 0; i < SPORT_POLL_ID_COUNT; i++) _idLastSeen[i] = 0;

        if (!_taskStarted) {
            _taskStarted = true;
            xTaskCreate(pollTaskThunk, "sportPoll", 3072, this, 6, nullptr);
        }
    }

    void end(HardwareSerial& serial) override {
        _masterActive = false;
        _pollEnabled = false;
        _inListenWindow = false;
        serial.end();
    }

    void update(TelemetryData& data) override {
        if (!_port) return;
        unsigned long now = millis();

        while (_port->available()) {
            _lastRxByteMs = now;
            data.lastByteMs = now;
            data.busRxSeen = true;
            feedByte(_port->read(), data);
        }

        serviceMaster(now, data);
        data.pollsSent = _pollsSent;

        if (now - _lastFpsTime >= 1000) {
            data.packetsPerSec = _fpsCounter;
            _fpsCounter = 0;
            _lastFpsTime = now;
        }
    }
};
