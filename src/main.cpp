#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>

#include "TelemetryBus.h"
#include "OledRenderer.h"
#include "SportParser.h"
#include "FutureParsers.h"

constexpr int PIN_OLED_SDA = 8;
constexpr int PIN_OLED_SCL = 10;
constexpr int PIN_SPORT_RX = 20;
constexpr int PIN_SPORT_TX = 21;
constexpr int PIN_BUTTON   = 9;

const char* AP_SSID = "SPort-HUD";
const char* AP_PASS = "12345678";

TelemetryData telem;
SystemConfig config;
Preferences prefs;
OledRenderer oled;

SportParser sportParser;
MspParser mspParser;
MavlinkParser mavlinkParser;
CrsfParser crsfParser;
JetiParser jetiParser;
ProtocolParser* activeParser = &sportParser;

AsyncWebServer server(80);
AsyncWebSocket ws("/ws");

unsigned long lastLoopMs = 0;
unsigned long lastWsPushMs = 0;
unsigned long lastOledMs = 0;
unsigned long lastBtnMs = 0;

void selectProtocol(ActiveProtocol proto) {
    if (activeParser) activeParser->end(Serial1);
    config.protocol = proto;
    switch (proto) {
        case ActiveProtocol::FRSKY_SPORT: activeParser = &sportParser; break;
        case ActiveProtocol::MSP_FC:      activeParser = &mspParser; break;
        case ActiveProtocol::MAVLINK:     activeParser = &mavlinkParser; break;
        case ActiveProtocol::CRSF:        activeParser = &crsfParser; break;
        case ActiveProtocol::JETI_EX:     activeParser = &jetiParser; break;
    }
    activeParser->begin(Serial1, PIN_SPORT_RX, PIN_SPORT_TX);
}

void saveConfig() {
    prefs.begin("hudcfg", false);
    prefs.putUChar("proto", (uint8_t)config.protocol);
    prefs.putUChar("preset", (uint8_t)config.oledPreset);
    prefs.putUChar("cells", config.manualCells);
    prefs.putBool("flip", config.flipOled);
    prefs.putString("theme", config.webTheme);
    for (int i = 0; i < 4; i++) {
        prefs.putString(("slot" + String(i)).c_str(), config.customOledSlots[i]);
    }
    prefs.putString("weblayout", config.customWebLayoutJson);
    prefs.end();
}

void loadConfig() {
    prefs.begin("hudcfg", true);
    config.protocol    = (ActiveProtocol)prefs.getUChar("proto", 0);
    config.oledPreset  = (OledPreset)prefs.getUChar("preset", 0);
    config.manualCells = prefs.getUChar("cells", 0);
    config.flipOled    = prefs.getBool("flip", false);
    config.webTheme    = prefs.getString("theme", "A1");
    for (int i = 0; i < 4; i++) {
        config.customOledSlots[i] = prefs.getString(("slot" + String(i)).c_str(), config.customOledSlots[i]);
    }
    config.customWebLayoutJson = prefs.getString("weblayout", "");
    prefs.end();
}

void autoDetectCellCount() {
    if (config.manualCells > 0) {
        telem.detectedCells = config.manualCells;
        return;
    }
    float v = telem.totalVoltage;
    if (v < 2.5f) return;
    uint8_t est = (uint8_t)ceil(v / 4.25f);
    telem.detectedCells = constrain(est, 1, 12);
}

void pushTelemetryWs() {
    if (ws.count() == 0) return;

    JsonDocument doc;
    doc["type"]    = "telem";
    doc["proto"]   = activeParser->name();
    doc["preset"]  = (uint8_t)config.oledPreset;
    doc["theme"]   = config.webTheme;
    doc["vfas1"]   = telem.vfas1;
    doc["vfas2"]   = telem.vfas2;
    doc["volt"]    = telem.totalVoltage;
    doc["cellV"]   = telem.cellVoltage;
    doc["cells"]   = telem.detectedCells;
    doc["minV"]    = telem.minVoltage;
    doc["curr"]    = telem.current;
    doc["peakI"]   = telem.peakCurrent;
    doc["pwr"]     = telem.powerWatts;
    doc["peakW"]   = telem.peakWatts;
    doc["mah"]     = telem.mahUsed;
    doc["rpm"]     = telem.rpm;
    doc["escT"]    = telem.escTemp;
    doc["motT"]    = telem.motorTemp;
    doc["alt"]     = telem.altitude;
    doc["vspd"]    = telem.vspeed;
    doc["spd"]     = telem.speedKmh;
    doc["roll"]    = telem.roll;
    doc["pitch"]   = telem.pitch;
    doc["hdg"]     = telem.heading;
    doc["rssi"]    = telem.rssi;
    doc["fps"]     = telem.packetsPerSec;
    doc["crcErr"]  = telem.crcErrors;
    doc["lastApp"] = telem.lastAppId;
    doc["lastPhy"] = telem.lastPhysId;

    String out;
    serializeJson(doc, out);
    ws.textAll(out);
}

void handleWsMessage(void* arg, uint8_t* data, size_t len) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!info->final || info->index != 0 || info->len != len || info->opcode != WS_TEXT) return;

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return;

    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "setConfig") == 0) {
        if (doc["protocol"].is<int>()) {
            selectProtocol((ActiveProtocol)doc["protocol"].as<int>());
        }
        if (doc["preset"].is<int>()) {
            config.oledPreset = (OledPreset)doc["preset"].as<int>();
        }
        if (doc["cells"].is<int>()) {
            config.manualCells = doc["cells"].as<uint8_t>();
        }
        if (doc["flipOled"].is<bool>()) {
            config.flipOled = doc["flipOled"].as<bool>();
        }
        if (doc["theme"].is<const char*>()) {
            config.webTheme = doc["theme"].as<String>();
        }
        if (doc["oledSlots"].is<JsonArray>()) {
            JsonArray arr = doc["oledSlots"].as<JsonArray>();
            for (int i = 0; i < 4 && i < (int)arr.size(); i++) {
                config.customOledSlots[i] = arr[i].as<String>();
            }
        }
        if (doc["webLayout"].is<String>()) {
            config.customWebLayoutJson = doc["webLayout"].as<String>();
        }
        saveConfig();
    } else if (strcmp(cmd, "resetPeaks") == 0) {
        telem.resetPeaks();
    }
}

void setup() {
    Serial.begin(115200);
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    LittleFS.begin(true);
    loadConfig();

    oled.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    selectProtocol(config.protocol);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS);

    ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType type, void* arg, uint8_t* data, size_t len) {
        if (type == WS_EVT_DATA) handleWsMessage(arg, data, len);
    });
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        if (LittleFS.exists("/index.html")) {
            req->send(LittleFS, "/index.html", "text/html");
        } else {
            req->send(200, "text/plain", "Upload index.html to LittleFS.");
        }
    });

    server.begin();
    lastLoopMs = millis();
}

void loop() {
    unsigned long now = millis();
    float dt = (now - lastLoopMs) / 1000.0f;
    lastLoopMs = now;

    activeParser->update(telem);
    autoDetectCellCount();
    telem.updateDerived(dt);

    if (digitalRead(PIN_BUTTON) == LOW && (now - lastBtnMs > 300)) {
        uint8_t next = ((uint8_t)config.oledPreset + 1) % 3;
        config.oledPreset = (OledPreset)next;
        saveConfig();
        lastBtnMs = now;
    }

    if (now - lastOledMs >= 50) {
        oled.render(telem, config, activeParser->name());
        lastOledMs = now;
    }

    if (now - lastWsPushMs >= 66) {
        ws.cleanupClients();
        pushTelemetryWs();
        lastWsPushMs = now;
    }
}
