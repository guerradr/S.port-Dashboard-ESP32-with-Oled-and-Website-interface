#include <Arduino.h>
#include <WiFi.h>
#include <LittleFS.h>
#include <Preferences.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <ESPmDNS.h>
#include <DNSServer.h>

#include "TelemetryBus.h"
#include "OledRenderer.h"
#include "SportParser.h"
#include "FutureParsers.h"

#define FW_VERSION "2.0.0"

#ifdef BOARD_LOLIN_C3_PICO
constexpr int PIN_OLED_SDA = 8;
constexpr int PIN_OLED_SCL = 10;
constexpr int PIN_SPORT_RX = 20;
constexpr int PIN_SPORT_TX = 21;
constexpr int PIN_BUTTON   = 9;
#define TELEM_SERIAL Serial1
#else
// ESP-WROOM-32 (esp32dev) default pinout
constexpr int PIN_OLED_SDA = 21;
constexpr int PIN_OLED_SCL = 22;
constexpr int PIN_SPORT_RX = 16;  // Serial2 RX
constexpr int PIN_SPORT_TX = 17;  // Serial2 TX (bench master polling)
constexpr int PIN_BUTTON   = 0;   // BOOT button (active LOW)
#define TELEM_SERIAL Serial2
#endif

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
DNSServer dnsServer;

unsigned long lastLoopMs = 0;
unsigned long lastWsPushMs = 0;
unsigned long lastOledMs = 0;

// OLED test pattern state (255 = off)
uint8_t oledTestMode = 255;
unsigned long oledTestUntil = 0;
uint8_t lastTestPattern = 255;

uint32_t lastRenderedFrames = 0;
uint8_t lastRenderedPreset = 255;

// ---------------------------------------------------------------------------
// Configuration persistence
// ---------------------------------------------------------------------------
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
    for (int i = 0; i < 3; i++) {
        prefs.putUChar(("am" + String(i)).c_str(), config.attMap[i]);
    }
    prefs.putBool("invP", config.invPitch);
    prefs.putBool("invR", config.invRoll);
    prefs.putBool("invY", config.invYaw);
    prefs.putUChar("busMode", config.busMode);
    prefs.putUChar("gpsUnit", config.gpsUnit);
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
        config.customOledSlots[i] = prefs.getString(("slot" + String(i)).c_str(),
                                                    config.customOledSlots[i]);
    }
    for (int i = 0; i < 3; i++) {
        config.attMap[i] = prefs.getUChar(("am" + String(i)).c_str(), config.attMap[i]);
        if (config.attMap[i] > 3) config.attMap[i] = 0;
    }
    config.invPitch = prefs.getBool("invP", false);
    config.invRoll  = prefs.getBool("invR", false);
    config.invYaw   = prefs.getBool("invY", false);
    config.busMode  = prefs.getUChar("busMode", 0);
    if (config.busMode > 2) config.busMode = 0;
    config.gpsUnit  = prefs.getUChar("gpsUnit", 0);
    if (config.gpsUnit > 1) config.gpsUnit = 0;
    prefs.end();

    if ((uint8_t)config.oledPreset >= OLED_PRESET_COUNT) {
        config.oledPreset = OledPreset::TELEMETRY_INFO;
    }
    if ((uint8_t)config.protocol > 4) config.protocol = ActiveProtocol::FRSKY_SPORT;
}

void selectProtocol(ActiveProtocol proto) {
    if (activeParser) activeParser->end(TELEM_SERIAL);
    config.protocol = proto;
    switch (proto) {
        case ActiveProtocol::FRSKY_SPORT: activeParser = &sportParser; break;
        case ActiveProtocol::MSP_FC:      activeParser = &mspParser; break;
        case ActiveProtocol::MAVLINK:     activeParser = &mavlinkParser; break;
        case ActiveProtocol::CRSF:        activeParser = &crsfParser; break;
        case ActiveProtocol::JETI_EX:     activeParser = &jetiParser; break;
    }
    activeParser->begin(TELEM_SERIAL, PIN_SPORT_RX, PIN_SPORT_TX);
    activeParser->setBusMode((BusMode)config.busMode);
}

// ---------------------------------------------------------------------------
// Telemetry post-processing
// ---------------------------------------------------------------------------
void autoDetectCellCount() {
    unsigned long now = millis();
    if (config.manualCells > 0) {
        telem.detectedCells = config.manualCells;
        return;
    }
    if (telem.cellsFresh(now) && telem.cellCount > 0) {
        telem.detectedCells = telem.cellCount;
        return;
    }
    float v = telem.totalVoltage;
    if (v < 2.5f) return;
    uint8_t est = (uint8_t)ceil(v / 4.25f);
    telem.detectedCells = (uint8_t)constrain((int)est, 1, 12);
}

void applyStationConfig(TelemetryData& d) {
    unsigned long now = millis();

    // GPS speed raw value: oXs sends knots x1000 by default, or km/h x100
    // when GPS_SPEED_IN_KMH is enabled in its config.
    if (d.speedRaw > 0.0f) {
        d.speedKmh = (config.gpsUnit == (uint8_t)GpsSpeedUnit::KMH)
                         ? d.speedRaw / 100.0f
                         : (d.speedRaw / 1000.0f) * 1.852f;
    }

    // openXsensor ACC slot mapping -> real attitude.
    bool freshAtt = false;
    for (int i = 0; i < 3; i++) {
        if (!d.accLastMs[i] || (now - d.accLastMs[i]) > 2000) continue;
        float v = d.accRaw[i] / 10.0f;  // oXs sends 0.1 degree units
        switch ((AttSlot)config.attMap[i]) {
            case AttSlot::PITCH:
                d.pitch = config.invPitch ? -v : v;
                freshAtt = true;
                break;
            case AttSlot::ROLL:
                d.roll = config.invRoll ? -v : v;
                freshAtt = true;
                break;
            case AttSlot::YAW:
                // GPS course wins while it is fresh (absolute heading);
                // the IMU yaw is used as a bench / standstill fallback.
                if (!(d.hdgGpsMs && (now - d.hdgGpsMs) < 10000)) {
                    float h = config.invYaw ? -v : v;
                    while (h < 0.0f) h += 360.0f;
                    while (h >= 360.0f) h -= 360.0f;
                    d.heading = h;
                    d.hdgImuMs = now;
                }
                break;
            default:
                break;
        }
    }
    if (freshAtt) d.imuAttLastMs = now;
}

// ---------------------------------------------------------------------------
// WebSocket helpers
// ---------------------------------------------------------------------------
const char* headingSrc(const TelemetryData& d) {
    unsigned long now = millis();
    if (d.hdgGpsMs && (now - d.hdgGpsMs) < 10000) return "GPS";
    if (d.hdgImuMs && (now - d.hdgImuMs) < 5000) return "IMU";
    return "NONE";
}

void pushConfig(AsyncWebSocketClient* client) {
    JsonDocument doc;
    doc["type"] = "cfg";
    doc["ver"] = FW_VERSION;
    doc["ssid"] = AP_SSID;
    doc["ip"] = WiFi.softAPIP().toString();
    doc["theme"] = config.webTheme;
    doc["preset"] = (uint8_t)config.oledPreset;
    doc["protocol"] = (uint8_t)config.protocol;
    doc["cells"] = config.manualCells;
    doc["flipOled"] = config.flipOled;
    JsonArray slots = doc["oledSlots"].to<JsonArray>();
    for (int i = 0; i < 4; i++) slots.add(config.customOledSlots[i]);
    JsonArray am = doc["attMap"].to<JsonArray>();
    for (int i = 0; i < 3; i++) am.add(config.attMap[i]);
    doc["invPitch"] = config.invPitch;
    doc["invRoll"] = config.invRoll;
    doc["invYaw"] = config.invYaw;
    doc["busMode"] = config.busMode;
    doc["gpsUnit"] = config.gpsUnit;
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = millis() / 1000;

    String out;
    serializeJson(doc, out);
    if (client) {
        ws.text(client->id(), out.c_str(), out.length());
    } else {
        ws.textAll(out.c_str(), out.length());
    }
}

void pushTelemetryWs() {
    unsigned long now = millis();
    bool cellsFresh = telem.cellsFresh(now);

    JsonDocument doc;
    doc["type"] = "telem";
    doc["ver"] = FW_VERSION;
    doc["proto"] = activeParser->name();
    doc["preset"] = (uint8_t)config.oledPreset;
    doc["theme"] = config.webTheme;

    doc["volt"] = telem.totalVoltage;
    doc["cellV"] = telem.cellVoltage;
    doc["cells"] = telem.detectedCells;
    doc["minV"] = telem.minVoltage;
    doc["cellMin"] = telem.minCellV;
    if (cellsFresh) {
        JsonArray arr = doc["cellv"].to<JsonArray>();
        for (uint8_t i = 0; i < telem.cellCount && i < MAX_CELLS; i++) {
            arr.add(telem.cellV[i]);
        }
    }

    doc["curr"] = telem.current;
    doc["peakI"] = telem.peakCurrent;
    doc["pwr"] = telem.powerWatts;
    doc["peakW"] = telem.peakWatts;
    doc["mah"] = telem.mahUsed;
    doc["rpm"] = telem.rpm;
    doc["escT"] = telem.escTemp;
    doc["motT"] = telem.motorTemp;

    doc["alt"] = telem.altitude;
    doc["maxAlt"] = telem.maxAltitude;
    doc["vspd"] = telem.vspeed;
    doc["spd"] = telem.speedKmh;
    doc["maxSpd"] = telem.maxSpeedKmh;
    doc["hdg"] = telem.heading;
    doc["roll"] = telem.roll;
    doc["pitch"] = telem.pitch;
    doc["lat"] = telem.gpsLat;
    doc["lon"] = telem.gpsLon;
    doc["gpsAlt"] = telem.gpsAlt;
    doc["sats"] = telem.gpsSats;
    doc["gpsFix"] = telem.gpsPosValid;

    doc["rssi"] = telem.rssi;
    doc["fps"] = telem.packetsPerSec;
    doc["crcErr"] = telem.crcErrors;
    doc["lastApp"] = telem.lastAppId;
    doc["lastPhy"] = telem.lastPhysId;

    doc["attSrc"] = attSourceLabel(telem, now);
    doc["hdgSrc"] = headingSrc(telem);
    doc["master"] = telem.busMasterActive;
    doc["polls"] = telem.pollsSent;
    doc["busMode"] = config.busMode;
    doc["heap"] = ESP.getFreeHeap();
    doc["uptime"] = now / 1000;

    String out;
    serializeJson(doc, out);
    ws.textAll(out.c_str(), out.length());
}

void handleWsMessage(AsyncWebSocketClient* client, void* arg, uint8_t* data, size_t len) {
    AwsFrameInfo* info = (AwsFrameInfo*)arg;
    if (!info->final || info->index != 0 || info->len != len ||
        info->opcode != WS_TEXT) {
        return;
    }

    JsonDocument doc;
    if (deserializeJson(doc, data, len)) return;

    const char* cmd = doc["cmd"] | "";
    if (strcmp(cmd, "setConfig") == 0) {
        if (doc["protocol"].is<int>()) {
            int p = doc["protocol"].as<int>();
            if (p >= 0 && p <= 4) selectProtocol((ActiveProtocol)p);
        }
        if (doc["preset"].is<int>()) {
            int p = doc["preset"].as<int>();
            if (p >= 0 && p < OLED_PRESET_COUNT) config.oledPreset = (OledPreset)p;
        }
        if (doc["cells"].is<int>()) {
            config.manualCells = (uint8_t)constrain(doc["cells"].as<int>(), 0, 12);
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
        if (doc["attMap"].is<JsonArray>()) {
            JsonArray arr = doc["attMap"].as<JsonArray>();
            for (int i = 0; i < 3 && i < (int)arr.size(); i++) {
                int v = arr[i].as<int>();
                config.attMap[i] = (uint8_t)((v >= 0 && v <= 3) ? v : 0);
            }
        }
        if (doc["invPitch"].is<bool>()) config.invPitch = doc["invPitch"].as<bool>();
        if (doc["invRoll"].is<bool>())  config.invRoll  = doc["invRoll"].as<bool>();
        if (doc["invYaw"].is<bool>())   config.invYaw   = doc["invYaw"].as<bool>();
        if (doc["busMode"].is<int>()) {
            int m = doc["busMode"].as<int>();
            if (m >= 0 && m <= 2) {
                config.busMode = (uint8_t)m;
                activeParser->setBusMode((BusMode)config.busMode);
            }
        }
        if (doc["gpsUnit"].is<int>()) {
            int u = doc["gpsUnit"].as<int>();
            if (u >= 0 && u <= 1) config.gpsUnit = (uint8_t)u;
        }
        saveConfig();
        pushConfig(nullptr);  // keep every connected client in sync
    } else if (strcmp(cmd, "resetPeaks") == 0) {
        telem.resetPeaks();
    } else if (strcmp(cmd, "oledTest") == 0) {
        int p = doc["pattern"].as<int>();
        if (p >= 0 && p <= 5) {
            oledTestMode = (uint8_t)p;
            oledTestUntil = millis() + 60000;
        }
    } else if (strcmp(cmd, "getCfg") == 0) {
        pushConfig(client);
    }
}

// ---------------------------------------------------------------------------
// HTTP
// ---------------------------------------------------------------------------
void serveIndex(AsyncWebServerRequest* req) {
    AsyncWebServerResponse* res = nullptr;
    if (LittleFS.exists("/index.html.gz")) {
        res = req->beginResponse(LittleFS, String("/index.html.gz"),
                                 String("text/html; charset=utf-8"));
        if (res) res->addHeader("Content-Encoding", "gzip");
    } else if (LittleFS.exists("/index.html")) {
        res = req->beginResponse(LittleFS, String("/index.html"),
                                 String("text/html; charset=utf-8"));
    }
    if (res) {
        res->addHeader("Cache-Control", "no-store");
        req->send(res);
    } else {
        req->send(200, "text/plain", "Upload index.html to LittleFS.");
    }
}

// ---------------------------------------------------------------------------
// setup / loop
// ---------------------------------------------------------------------------
void setup() {
    Serial.begin(115200);
    pinMode(PIN_BUTTON, INPUT_PULLUP);

    LittleFS.begin(true);
    loadConfig();

    oled.begin(PIN_OLED_SDA, PIN_OLED_SCL);
    selectProtocol(config.protocol);

    WiFi.mode(WIFI_AP);
    WiFi.softAP(AP_SSID, AP_PASS, 1, 0, 4);

    if (MDNS.begin("sport-hud")) {
        MDNS.addService("http", "tcp", 80);
    }

    dnsServer.start(53, "*", WiFi.softAPIP());

    ws.onEvent([](AsyncWebSocket* s, AsyncWebSocketClient* c, AwsEventType type,
                  void* arg, uint8_t* data, size_t len) {
        (void)s;
        if (type == WS_EVT_CONNECT) {
            pushConfig(c);
        } else if (type == WS_EVT_DATA) {
            handleWsMessage(c, arg, data, len);
        }
    });
    server.addHandler(&ws);

    server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) { serveIndex(req); });
    server.onNotFound([](AsyncWebServerRequest* req) {
        if (req->method() == HTTP_GET) {
            serveIndex(req);  // captive portal friendliness
        } else {
            req->send(404);
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
    applyStationConfig(telem);
    autoDetectCellCount();
    telem.updateDerived(dt);

    // ---- Button: short press = next preset, long press = OLED self test ----
    static unsigned long pressStartMs = 0;
    static bool longPressFired = false;
    int btn = digitalRead(PIN_BUTTON);
    if (btn == LOW) {
        if (pressStartMs == 0) {
            pressStartMs = now;
        } else if (!longPressFired && (now - pressStartMs) > 1200) {
            longPressFired = true;
            oledTestMode = (oledTestMode == 255) ? 0 : 255;
            if (oledTestMode != 255) oledTestUntil = now + 60000;
        }
    } else if (pressStartMs != 0) {
        unsigned long held = now - pressStartMs;
        if (!longPressFired && held > 30) {
            if (oledTestMode != 255) {
                oledTestMode = (oledTestMode + 1) % 6;
                oledTestUntil = now + 60000;
            } else {
                config.oledPreset =
                    (OledPreset)(((uint8_t)config.oledPreset + 1) % OLED_PRESET_COUNT);
                saveConfig();
            }
        }
        pressStartMs = 0;
        longPressFired = false;
    }

    if (oledTestMode != 255 && (int32_t)(now - oledTestUntil) > 0) {
        oledTestMode = 255;
    }

    // ---- OLED: dirty-flag rendering at max 10 fps -------------------------
    if (now - lastOledMs >= 100) {
        if (oledTestMode != 255) {
            if (oledTestMode != lastTestPattern || now - lastOledMs >= 1000) {
                oled.renderTestPattern(oledTestMode);
                lastTestPattern = oledTestMode;
                lastOledMs = now;
            }
        } else if (telem.framesDecoded != lastRenderedFrames ||
                   (uint8_t)config.oledPreset != lastRenderedPreset ||
                   now - lastOledMs >= 1000) {
            oled.render(telem, config, activeParser->name());
            lastRenderedFrames = telem.framesDecoded;
            lastRenderedPreset = (uint8_t)config.oledPreset;
            lastOledMs = now;
        }
    }

    // ---- WebSocket: ~15 Hz telemetry push ---------------------------------
    if (now - lastWsPushMs >= 66) {
        ws.cleanupClients();
        if (ws.count() > 0) pushTelemetryWs();
        lastWsPushMs = now;
    }

    dnsServer.processNextRequest();
}
