#pragma once
#include <Arduino.h>
#include "TelemetryBus.h"

class ProtocolParser {
public:
    virtual ~ProtocolParser() {}
    virtual const char* name() const = 0;
    virtual void begin(HardwareSerial& serial, int rxPin, int txPin) = 0;
    virtual void update(TelemetryData& data) = 0;
    virtual void end(HardwareSerial& serial) { serial.end(); }
};
