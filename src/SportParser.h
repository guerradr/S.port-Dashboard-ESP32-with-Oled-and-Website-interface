#pragma once
#include "ProtocolParser.h"

static constexpr uint8_t SPORT_START_STOP = 0x7E;
static constexpr uint8_t SPORT_BYTESTUFF  = 0x7D;
static constexpr uint8_t SPORT_STUFF_MASK = 0x20;
static constexpr uint8_t SPORT_DATA_FRAME = 0x10;

static constexpr uint16_t APPID_ALT_FIRST   = 0x0100;
static constexpr uint16_t APPID_VARIO_FIRST = 0x0110;
static constexpr uint16_t APPID_CURR_FIRST  = 0x0200;
static constexpr uint16_t APPID_VFAS_FIRST  = 0x0210;
static constexpr uint16_t APPID_VFAS_SECOND = 0x0211;
static constexpr uint16_t APPID_TEMP1_FIRST = 0x0400;
static constexpr uint16_t APPID_TEMP2_FIRST = 0x0410;
static constexpr uint16_t APPID_RPM_FIRST   = 0x0500;
static constexpr uint16_t APPID_GPS_SPEED   = 0x0830;
static constexpr uint16_t APPID_GPS_COURSE  = 0x0840;
static constexpr uint16_t APPID_RSSI        = 0xF101;

class SportParser : public ProtocolParser {
private:
    HardwareSerial* _port = nullptr;
    uint8_t _buf[9];
    uint8_t _idx = 0;
    bool _inFrame = false;
    bool _stuffed = false;
    uint32_t _fpsCounter = 0;
    unsigned long _lastFpsTime = 0;

    bool checkCrc(const uint8_t* packet) {
        uint16_t crc = 0;
        for (int i = 1; i <= 7; i++) {
            crc += packet[i];
            crc += crc >> 8;
            crc &= 0x00FF;
        }
        return ((0xFF - crc) == packet[8]);
    }

    void decodePayload(uint8_t physId, uint16_t appId, int32_t val, TelemetryData& d) {
        d.lastPhysId = physId;
        d.lastAppId = appId;
        d.lastRawValue = (uint32_t)val;
        d.lastPacketMs = millis();

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
        } else if (baseId == APPID_GPS_SPEED) {
            d.speedKmh = (val / 1000.0f) * 1.852f;
            d.activeSensorMask |= (1 << 7);
        } else if (baseId == APPID_GPS_COURSE) {
            d.heading = val / 100.0f;
        } else if (appId == APPID_RSSI) {
            d.rssi = (int)(val & 0xFF);
            d.activeSensorMask |= (1 << 8);
        }
    }

public:
    const char* name() const override { return "FrSky S.Port"; }

    void begin(HardwareSerial& serial, int rxPin, int txPin) override {
        _port = &serial;
        _port->begin(57600, SERIAL_8N1, rxPin, txPin);
        _port->setRxInvert(true);
        _port->setTxInvert(true);
        _idx = 0;
        _inFrame = false;
        _stuffed = false;
        _lastFpsTime = millis();
    }

    void update(TelemetryData& data) override {
        if (!_port) return;

        while (_port->available()) {
            uint8_t b = _port->read();

            if (b == SPORT_START_STOP) {
                _idx = 0;
                _inFrame = true;
                _stuffed = false;
                continue;
            }

            if (!_inFrame) continue;

            if (b == SPORT_BYTESTUFF) {
                _stuffed = true;
                continue;
            }

            if (_stuffed) {
                b ^= SPORT_STUFF_MASK;
                _stuffed = false;
            }

            if (_idx < 9) {
                _buf[_idx++] = b;
            }

            if (_idx == 9) {
                _inFrame = false;
                if (_buf[1] == SPORT_DATA_FRAME) {
                    if (checkCrc(_buf)) {
                        uint16_t appId = (uint16_t)_buf[2] | ((uint16_t)_buf[3] << 8);
                        int32_t value  = (int32_t)((uint32_t)_buf[4] |
                                         ((uint32_t)_buf[5] << 8) |
                                         ((uint32_t)_buf[6] << 16) |
                                         ((uint32_t)_buf[7] << 24));
                        decodePayload(_buf[0], appId, value, data);
                        data.validPackets++;
                        _fpsCounter++;
                    } else {
                        data.crcErrors++;
                    }
                }
            }
        }

        unsigned long now = millis();
        if (now - _lastFpsTime >= 1000) {
            data.packetsPerSec = _fpsCounter;
            _fpsCounter = 0;
            _lastFpsTime = now;
        }
    }
};
