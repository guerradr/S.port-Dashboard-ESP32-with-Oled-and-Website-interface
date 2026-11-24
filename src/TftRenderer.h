#pragma once
#include <Arduino.h>
#include <SPI.h>
#include <TFT_eSPI.h>
#if __has_include(<XPT2046_Touchscreen.h>)
#include <XPT2046_Touchscreen.h>
#else
#include <XPT2046_Touchscreen_TT.h>
#endif
#include <math.h>
#include "TelemetryBus.h"

// ---------------------------------------------------------------------------
// TftRenderer — ESP32-2443S028R "Cheap Yellow Display" UI (v2.3.0)
//
// 320x240 landscape, five pages, touch tab bar + BOOT navigation.
// Full-page redraw is throttled (~8 fps) because a 320x240 fill at SPI 40 MHz
// costs ~30 ms; S.Port parsing runs in the main loop and stays responsive.
// ---------------------------------------------------------------------------

static constexpr int16_t TFT_W = 320;
static constexpr int16_t TFT_H = 240;
static constexpr int16_t TAB_H = 24;
static constexpr int16_t TFT_CY = (TFT_H - TAB_H) / 2;

static constexpr uint8_t PAGE_HUD = 0;
static constexpr uint8_t PAGE_PFD = 1;
static constexpr uint8_t PAGE_DRIVE = 2;
static constexpr uint8_t PAGE_GPS = 3;
static constexpr uint8_t PAGE_DIAG = 4;
static constexpr uint8_t PAGE_COUNT = 5;

// XPT2046 raw range (typical CYD; refine via the DBG page readout if needed)
static constexpr int TOUCH_RAW_MIN = 200;
static constexpr int TOUCH_RAW_MAX = 3900;

static constexpr uint16_t TRAIL_MAX = 280;

struct GpsTrailPt {
    int16_t x;  // meters east of home
    int16_t y;  // meters north of home
};

class TftRenderer {
public:
    TftRenderer() : _touch(33, 36) {}

    bool begin() {
        _tft.init();
        _tft.setRotation(1);  // landscape 320x240
        _tft.setTextWrap(false);
        buildPalette();
        _tft.fillScreen(TFT_BLACK);

        _touchSpi.begin(25, 39, 32, 33);
        _touch.begin(_touchSpi);
        _touch.setRotation(1);

        _lastDraw = 0;
        _lastTouchPoll = 0;
        return true;
    }

    uint8_t page() const { return _page; }
    void setPage(uint8_t p) { _page = (uint8_t)(p % PAGE_COUNT); _forceRedraw = true; }
    void nextPage() { setPage((uint8_t)(_page + 1)); }

    void update(const TelemetryData& d, const SystemConfig& cfg, const char* proto,
                const char* version, uint8_t testPattern) {
        unsigned long now = millis();
        _stallSpd = cfg.stallSpd;
        _minAlt = cfg.minAlt;

        if (testPattern != 255) {
            if (testPattern != _lastTest || (now - _lastDraw) > 1000) {
                drawTest(testPattern);
                _lastTest = testPattern;
                _lastDraw = now;
            }
            return;
        }
        _lastTest = 255;

        if (pollTouch()) _forceRedraw = true;

        if (!_forceRedraw && (now - _lastDraw) < 120 &&
            d.framesDecoded == _lastFrames) {
            return;
        }
        if (!_forceRedraw && d.framesDecoded == _lastFrames &&
            (now - _lastDraw) < 1000) {
            return;
        }

        _forceRedraw = false;
        _lastFrames = d.framesDecoded;
        _lastDraw = now;
        _accent = accentFor(cfg.webTheme);

        switch (_page) {
            case PAGE_HUD:   drawPageHud(d); break;
            case PAGE_PFD:   drawPagePfd(d); break;
            case PAGE_DRIVE: drawPageDrive(d); break;
            case PAGE_GPS:   drawPageGps(d); break;
            default:         drawPageDiag(d, cfg, proto, version); break;
        }
        drawBottomBar();
    }

private:
    TFT_eSPI _tft;
    SPIClass _touchSpi = SPIClass(VSPI);
    XPT2046_Touchscreen _touch;

    uint8_t _page = PAGE_HUD;
    unsigned long _lastDraw = 0;
    unsigned long _lastTouchPoll = 0;
    uint32_t _lastFrames = 0;
    uint8_t _lastTest = 255;
    bool _forceRedraw = true;

    uint8_t _stallSpd = 35;
    uint16_t _minAlt = 30;

    uint16_t _accent = 0x07E0;
    uint16_t _skyTop = 0, _skyBot = 0, _gndTop = 0, _gndBot = 0;
    uint16_t _panel = 0, _muted = 0, _warn = 0, _danger = 0, _text = 0xFFFF;

    int16_t _lastTouchX = -1, _lastTouchY = -1;
    int _lastRawX = 0, _lastRawY = 0;

    bool _homeSet = false;
    float _homeLat = 0, _homeLon = 0;
    GpsTrailPt _trail[TRAIL_MAX];
    uint16_t _trailCount = 0;
    unsigned long _lastTrailMs = 0;
    int16_t _trailLastX = 0, _trailLastY = 0;

    // ------------------------------------------------------------------
    void buildPalette() {
        _skyTop = _tft.color565(43, 108, 176);
        _skyBot = _tft.color565(19, 44, 82);
        _gndTop = _tft.color565(123, 63, 32);
        _gndBot = _tft.color565(47, 26, 14);
        _panel = _tft.color565(20, 28, 38);
        _muted = _tft.color565(143, 162, 184);
        _warn = _tft.color565(245, 158, 11);
        _danger = _tft.color565(239, 68, 68);
        _text = TFT_WHITE;
    }

    uint16_t accentFor(const String& theme) {
        if (theme == "A2") return _tft.color565(96, 165, 250);
        if (theme == "A3") return _tft.color565(234, 179, 8);
        if (theme == "B")  return _tft.color565(255, 42, 85);
        if (theme == "C")  return _tft.color565(0, 229, 255);
        return _tft.color565(57, 255, 122);  // A1 jet green
    }

    static String signedStr(int v) {
        return String(v >= 0 ? "+" : "") + String(v);
    }

    // ------------------------------------------------------------------
    bool pollTouch() {
        unsigned long now = millis();
        if (now - _lastTouchPoll < 60) return false;
        _lastTouchPoll = now;
        if (!_touch.touched()) return false;

        TS_Point p = _touch.getPoint();
        _lastRawX = p.x;
        _lastRawY = p.y;

        int16_t x = (int16_t)map(constrain((int)p.x, TOUCH_RAW_MIN, TOUCH_RAW_MAX),
                                 TOUCH_RAW_MIN, TOUCH_RAW_MAX, 0, TFT_W - 1);
        int16_t y = (int16_t)map(constrain((int)p.y, TOUCH_RAW_MIN, TOUCH_RAW_MAX),
                                 TOUCH_RAW_MIN, TOUCH_RAW_MAX, 0, TFT_H - 1);
        _lastTouchX = x;
        _lastTouchY = y;

        if (y >= TFT_H - TAB_H) {
            uint8_t page = (uint8_t)(x / (TFT_W / PAGE_COUNT));
            if (page >= PAGE_COUNT) page = PAGE_COUNT - 1;
            if (page != _page) {
                _page = page;
                return true;
            }
        }
        return false;
    }

    void drawBottomBar() {
        static const char* labels[PAGE_COUNT] = {"HUD", "PFD", "DRIVE", "GPS", "DBG"};
        int16_t w = TFT_W / PAGE_COUNT;
        _tft.fillRect(0, TFT_H - TAB_H, TFT_W, TAB_H, _panel);
        _tft.setTextFont(2);
        _tft.setTextSize(1);
        for (uint8_t i = 0; i < PAGE_COUNT; i++) {
            int16_t x = (int16_t)(i * w);
            bool active = (i == _page);
            if (active) _tft.fillRect(x, TFT_H - TAB_H, w, TAB_H, _accent);
            _tft.setTextColor(active ? TFT_BLACK : _muted);
            _tft.setTextDatum(MC_DATUM);
            _tft.drawString(labels[i], x + w / 2, TFT_H - TAB_H / 2 + 1);
        }
        _tft.setTextDatum(TL_DATUM);
    }

    // ------------------------------------------------------------------
    // Page 1: Jet HUD (Gripen-style)
    // ------------------------------------------------------------------
    void drawPageHud(const TelemetryData& d) {
        _tft.fillRect(0, 0, TFT_W, TFT_H - TAB_H, TFT_BLACK);

        const int16_t cx = TFT_W / 2;
        const int16_t cy = TFT_CY;
        const float rollRad = (-d.roll * 0.0174533f);
        const float cr = cosf(rollRad), sr = sinf(rollRad);
        const float pxd = 2.2f;
        auto px = [&](float u, float v) -> int16_t { return (int16_t)(cx + u * cr + v * sr); };
        auto py = [&](float u, float v) -> int16_t { return (int16_t)(cy - u * sr + v * cr); };

        drawCompassTape(d.heading);

        // Ladder rungs every 10 deg with end ticks toward the horizon.
        for (int deg = -40; deg <= 40; deg += 10) {
            if (deg == 0) continue;
            float v = (d.pitch - deg) * pxd;
            if (v < -46 || v > 46) continue;
            float half = 40;
            _tft.drawLine(px(-half, v), py(-half, v), px(half, v), py(half, v), _accent);
            float tick = (deg > 0) ? 3.5f : -3.5f;
            _tft.drawLine(px(-half, v), py(-half, v), px(-half, v + tick),
                          py(-half, v + tick), _accent);
            _tft.drawLine(px(half, v), py(half, v), px(half, v + tick),
                          py(half, v + tick), _accent);
        }

        // Horizon with a center gap.
        float v0 = d.pitch * pxd;
        _tft.drawLine(px(-160, v0), py(-160, v0), px(-22, v0), py(-22, v0), _accent);
        _tft.drawLine(px(22, v0), py(22, v0), px(160, v0), py(160, v0), _accent);

        // Boresight ghost (fixed).
        _tft.drawLine(cx - 20, cy, cx - 8, cy, _accent);
        _tft.drawLine(cx - 8, cy, cx - 4, cy + 6, _accent);
        _tft.drawLine(cx + 4, cy + 6, cx + 8, cy, _accent);
        _tft.drawLine(cx + 8, cy, cx + 20, cy, _accent);

        // Flight path vector.
        float spdMs = d.speedKmh / 3.6f;
        float fpa = (spdMs > 2) ? atan2f(d.vspeed, spdMs) * 57.2958f : 0;
        float vf = (d.pitch - fpa) * pxd;
        int16_t fx = px(0, vf), fy = py(0, vf);
        if (fx > 30 && fx < TFT_W - 30 && fy > 30 && fy < TFT_H - TAB_H - 24) {
            _tft.drawCircle(fx, fy, 6, _accent);
            _tft.drawFastHLine(fx - 18, fy, 6, _accent);
            _tft.drawFastHLine(fx + 12, fy, 6, _accent);
        }

        // Ten-dot dials (speed left, altitude + vario right).
        drawTenDotDial(40, cy, 30, d.speedKmh, String((int)d.speedKmh), "KM/H");
        drawTenDotDial(TFT_W - 40, cy, 30, d.altitude, String((int)d.altitude),
                       String(d.vspeed, 1) + "m/s");

        // Roll / pitch readout.
        _tft.setTextFont(2);
        _tft.setTextColor(_accent, TFT_BLACK);
        _tft.setTextDatum(TL_DATUM);
        _tft.drawString("R " + signedStr((int)d.roll), 6, TFT_H - TAB_H - 22);
        _tft.setTextDatum(TR_DATUM);
        _tft.drawString("P " + signedStr((int)d.pitch), TFT_W - 6, TFT_H - TAB_H - 22);

        // Alerts.
        const char* alert = jetAlert(d);
        if (alert) {
            _tft.fillRect(0, cy + 36, TFT_W, 22, TFT_BLACK);
            _tft.setTextColor(_danger, TFT_BLACK);
            _tft.setTextDatum(MC_DATUM);
            _tft.drawString(alert, TFT_W / 2, cy + 47);
        }
        _tft.setTextDatum(TL_DATUM);
    }

    const char* jetAlert(const TelemetryData& d) {
        if (d.speedKmh > 5 && d.speedKmh < (float)_stallSpd) return "STALL SPEED";
        if (d.altitude < (float)_minAlt && d.speedKmh > 15) return "PULL UP / LOW ALT";
        return nullptr;
    }

    void drawCompassTape(float hdg) {
        const int16_t cx = TFT_W / 2;
        const int16_t top = 4;
        const int16_t w = 220;
        _tft.fillRect(cx - w / 2, top, w, 26, TFT_BLACK);
        _tft.setTextFont(1);
        _tft.setTextDatum(TC_DATUM);

        int16_t base = (int16_t)(hdg / 5.0f) * 5;
        for (int16_t b = base - 50; b <= base + 50; b += 5) {
            int16_t x = (int16_t)(cx + (b - hdg) * 2.1f);
            if (x < cx - w / 2 + 2 || x > cx + w / 2 - 2) continue;
            bool major = (b % 15) == 0;
            _tft.drawFastVLine(x, top + (major ? 15 : 19), major ? 8 : 4,
                               major ? _accent : _muted);
            if (major) {
                int16_t norm = ((b % 360) + 360) % 360;
                String lbl = (norm == 0) ? String("N") : String(norm / 10);
                _tft.setTextColor(major ? _accent : _muted, TFT_BLACK);
                _tft.drawString(lbl, x, top + 2);
            }
        }
        _tft.fillTriangle(cx - 5, top + 24, cx + 5, top + 24, cx, top + 30, _accent);
        _tft.setTextDatum(TL_DATUM);
    }

    void drawTenDotDial(int16_t dcx, int16_t dcy, int16_t r, float value,
                        const String& mainTxt, const String& subTxt) {
        _tft.fillCircle(dcx, dcy, r + 8, _panel);
        _tft.drawCircle(dcx, dcy, r + 8, _accent);

        for (uint8_t i = 0; i < 10; i++) {
            float a = (i / 10.0f) * 6.2831853f;
            _tft.fillCircle(dcx + (int16_t)(sinf(a) * r),
                            dcy - (int16_t)(cosf(a) * r), 2, _accent);
        }

        float v = fabsf(value);
        v = fmodf(v, 100.0f) / 100.0f;
        float pa = v * 6.2831853f;
        _tft.drawLine(dcx + (int16_t)(sinf(pa) * (r - 8)),
                      dcy - (int16_t)(cosf(pa) * (r - 8)),
                      dcx + (int16_t)(sinf(pa) * (r + 3)),
                      dcy - (int16_t)(cosf(pa) * (r + 3)), _accent);

        _tft.setTextFont(4);
        _tft.setTextColor(TFT_WHITE, _panel);
        _tft.setTextDatum(MC_DATUM);
        _tft.drawString(mainTxt, dcx, dcy);
        _tft.setTextFont(1);
        _tft.setTextColor(_muted, _panel);
        _tft.drawString(subTxt, dcx, dcy + r + 11);
        _tft.setTextDatum(TL_DATUM);
    }

    // ------------------------------------------------------------------
    // Page 2: PFD with sky/ground, tapes, bank scale
    // ------------------------------------------------------------------
    void drawPagePfd(const TelemetryData& d) {
        const int16_t cx = TFT_W / 2;
        const int16_t cy = TFT_CY;
        const float rollRad = (-d.roll * 0.0174533f);
        const float cr = cosf(rollRad), sr = sinf(rollRad);
        const float pxd = 2.4f;
        auto px = [&](float u, float v) -> int16_t { return (int16_t)(cx + u * cr + v * sr); };
        auto py = [&](float u, float v) -> int16_t { return (int16_t)(cy - u * sr + v * cr); };
        float v0 = d.pitch * pxd;

        int16_t sx1 = px(-400, v0 - 800), sy1 = py(-400, v0 - 800);
        int16_t sx2 = px(400, v0 - 800), sy2 = py(400, v0 - 800);
        int16_t sx3 = px(400, v0), sy3 = py(400, v0);
        int16_t sx4 = px(-400, v0), sy4 = py(-400, v0);
        _tft.fillTriangle(sx1, sy1, sx2, sy2, sx3, sy3, _skyBot);
        _tft.fillTriangle(sx1, sy1, sx3, sy3, sx4, sy4, _skyTop);

        int16_t gx1 = sx4, gy1 = sy4;
        int16_t gx2 = sx3, gy2 = sy3;
        int16_t gx3 = px(400, v0 + 800), gy3 = py(400, v0 + 800);
        int16_t gx4 = px(-400, v0 + 800), gy4 = py(-400, v0 + 800);
        _tft.fillTriangle(gx1, gy1, gx2, gy2, gx3, gy3, _gndTop);
        _tft.fillTriangle(gx1, gy1, gx3, gy3, gx4, gy4, _gndBot);

        _tft.drawLine(px(-400, v0), py(-400, v0), px(400, v0), py(400, v0), TFT_WHITE);

        _tft.setTextFont(1);
        _tft.setTextDatum(MC_DATUM);
        for (int deg = -60; deg <= 60; deg += 5) {
            if (deg == 0) continue;
            float v = (d.pitch - deg) * pxd;
            if (v < -95 || v > 95) continue;
            bool major = (deg % 10) == 0;
            float half = major ? 46 : 26;
            _tft.drawLine(px(-half, v), py(-half, v), px(half, v), py(half, v),
                          major ? TFT_WHITE : _muted);
            if (major) {
                char buf[8];
                snprintf(buf, sizeof(buf), "%d", deg < 0 ? -deg : deg);
                uint16_t bg = (deg > 0) ? _skyBot : _gndTop;
                _tft.setTextColor(TFT_WHITE, bg);
                _tft.drawString(buf, px(-half - 12, v), py(-half - 12, v));
                _tft.drawString(buf, px(half + 12, v), py(half + 12, v));
            }
        }

        // Bank scale (fixed ticks) + rolling pointer.
        for (int b = -60; b <= 60; b += 10) {
            if (b == 0) continue;
            float a = (b - 90) * 0.0174533f;
            _tft.drawLine(cx + (int16_t)(sinf(a) * 86),
                          cy - (int16_t)(cosf(a) * 86),
                          cx + (int16_t)(sinf(a) * 94),
                          cy - (int16_t)(cosf(a) * 94), _muted);
        }
        float pa = (d.roll - 90) * 0.0174533f;
        int16_t bx = cx + (int16_t)(sinf(pa) * 84);
        int16_t by = cy - (int16_t)(cosf(pa) * 84);
        _tft.fillTriangle(bx + (int16_t)(cosf(pa) * 4),
                          by + (int16_t)(sinf(pa) * 4),
                          bx - (int16_t)(cosf(pa) * 4),
                          by - (int16_t)(sinf(pa) * 4),
                          cx + (int16_t)(sinf(pa) * 74),
                          cy - (int16_t)(cosf(pa) * 74), _accent);

        // Fixed aircraft symbol.
        _tft.drawLine(cx - 44, cy, cx - 14, cy, _warn);
        _tft.drawLine(cx - 14, cy, cx - 10, cy + 6, _warn);
        _tft.drawLine(cx + 44, cy, cx + 14, cy, _warn);
        _tft.drawLine(cx + 14, cy, cx + 10, cy + 6, _warn);
        _tft.fillRect(cx - 2, cy - 2, 4, 4, _warn);

        drawTape(4, "SPD", d.speedKmh, 10, 20);
        drawTape(TFT_W - 52, "ALT", d.altitude, 25, 50);
        _tft.setTextDatum(TL_DATUM);
    }

    void drawTape(int16_t x, const char* label, float value, float minor, float major) {
        const int16_t w = 48;
        const int16_t top = 30;
        const int16_t h = 150;
        _tft.fillRect(x, top, w, h, _panel);
        _tft.drawRect(x, top, w, h, _muted);
        _tft.setTextFont(1);
        _tft.setTextColor(_muted, _panel);
        _tft.setTextDatum(TC_DATUM);
        _tft.drawString(label, x + w / 2, 12);

        float pxPerUnit = 2.2f;
        float first = floorf((value - (h / pxPerUnit) / 2.0f) / minor) * minor;
        for (float v = first; v <= value + (h / pxPerUnit); v += minor) {
            int16_t yy = top + h / 2 - (int16_t)((v - value) * pxPerUnit);
            if (yy < top + 2 || yy > top + h - 2) continue;
            bool isMajor = (fmodf(v, major) < 0.01f) || (fmodf(v, major) > major - 0.01f);
            _tft.drawFastHLine(x + 2 + (isMajor ? 8 : 16), yy, isMajor ? 12 : 6,
                               isMajor ? _text : _muted);
        }

        _tft.fillRect(x + 2, top + h / 2 - 12, w - 4, 22, _accent);
        _tft.setTextFont(2);
        _tft.setTextColor(TFT_BLACK, _accent);
        _tft.setTextDatum(MC_DATUM);
        _tft.drawString(String((int)value), x + w / 2, top + h / 2);
        _tft.setTextDatum(TL_DATUM);
    }

    // ------------------------------------------------------------------
    // Page 3: Drivetrain dashboard
    // ------------------------------------------------------------------
    void drawPageDrive(const TelemetryData& d) {
        _tft.fillRect(0, 0, TFT_W, TFT_H - TAB_H, TFT_BLACK);

        drawValueBox(4, 4, 100, 62, "VOLT", String(d.totalVoltage, 1) + "V", _accent);
        drawValueBox(110, 4, 100, 62, "CURR", String(d.current, 1) + "A", _accent);
        drawValueBox(216, 4, 100, 62, "POWER", String((int)d.powerWatts) + "W", _accent);
        drawValueBox(4, 72, 100, 62, "RPM", String((int)d.rpm), _accent);
        drawValueBox(110, 72, 100, 62, "mAh", String((int)d.mahUsed), _accent);
        drawValueBox(216, 72, 100, 62, "SAG", String(d.sagPackV, 2) + "V", _warn);

        drawCellsRow(d, 4, 140);

        drawBarGauge(4, 176, 150, 10, "ESC", d.escTemp, 20, 100);
        drawBarGauge(166, 176, 150, 10, "MOT", d.motorTemp, 20, 100);

        _tft.setTextFont(1);
        _tft.setTextColor(_muted, TFT_BLACK);
        _tft.setTextDatum(TL_DATUM);
        _tft.drawString("R " + String(d.resMohm, 1) + " mOhm", 4, 198);
        _tft.setTextDatum(TR_DATUM);
        _tft.drawString("PK " + String((int)d.peakWatts) + "W " +
                        String(d.peakCurrent, 0) + "A", TFT_W - 4, 198);
        _tft.setTextDatum(TL_DATUM);
    }

    void drawValueBox(int16_t x, int16_t y, int16_t w, int16_t h,
                      const char* label, const String& value, uint16_t color) {
        _tft.fillRect(x, y, w, h, _panel);
        _tft.drawRect(x, y, w, h, _muted);
        _tft.setTextFont(1);
        _tft.setTextColor(_muted, _panel);
        _tft.setTextDatum(TL_DATUM);
        _tft.drawString(label, x + 4, y + 3);
        _tft.setTextFont(4);
        _tft.setTextColor(color, _panel);
        _tft.setTextDatum(MC_DATUM);
        _tft.drawString(value, x + w / 2, y + h / 2 + 8);
        _tft.setTextDatum(TL_DATUM);
    }

    void drawCellsRow(const TelemetryData& d, int16_t x, int16_t y) {
        _tft.setTextFont(1);
        _tft.setTextColor(_muted, TFT_BLACK);
        _tft.setTextDatum(TL_DATUM);
        _tft.drawString("CELLS", x, y);

        const int16_t bw = 30, gap = 4;
        const int16_t by = y + 12, bh = 26;
        for (uint8_t i = 0; i < 6; i++) {
            int16_t bx = x + i * (bw + gap);
            float v = (d.cellsValid && i < d.cellCount) ? d.cellV[i] : 0.0f;
            _tft.drawRect(bx, by, bw, bh, _muted);
            if (v > 0.5f) {
                float f = (v - 3.0f) / 1.25f;
                f = f < 0 ? 0 : (f > 1 ? 1 : f);
                _tft.fillRect(bx + 1, by + 1, (int16_t)((bw - 2) * f), bh - 2,
                              v < 3.5f ? _danger : _accent);
            }
            char buf[6];
            snprintf(buf, sizeof(buf), "%.2f", v);
            _tft.setTextDatum(TC_DATUM);
            _tft.setTextColor(_text, TFT_BLACK);
            _tft.drawString(buf, bx + bw / 2, by + bh + 2);
            _tft.setTextDatum(TL_DATUM);
        }
    }

    void drawBarGauge(int16_t x, int16_t y, int16_t w, int16_t h,
                      const char* label, float value, float lo, float hi) {
        _tft.setTextFont(1);
        _tft.setTextColor(_muted, TFT_BLACK);
        _tft.setTextDatum(TL_DATUM);
        _tft.drawString(label, x, y - 10);
        _tft.drawRect(x, y, w, h, _muted);
        float f = (value - lo) / (hi - lo);
        f = f < 0 ? 0 : (f > 1 ? 1 : f);
        _tft.fillRect(x + 1, y + 1, (int16_t)((w - 2) * f), h - 2,
                      f > 0.75f ? _danger : _accent);
        _tft.setTextDatum(TR_DATUM);
        _tft.setTextColor(_text, TFT_BLACK);
        _tft.drawString(String((int)value) + "C", x + w, y - 10);
        _tft.setTextDatum(TL_DATUM);
    }

    // ------------------------------------------------------------------
    // Page 4: GPS track map
    // ------------------------------------------------------------------
    void drawPageGps(const TelemetryData& d) {
        _tft.fillRect(0, 0, TFT_W, TFT_H - TAB_H, TFT_BLACK);
        updateTrail(d);

        if (!_homeSet) {
            _tft.setTextFont(2);
            _tft.setTextColor(_muted, TFT_BLACK);
            _tft.setTextDatum(MC_DATUM);
            _tft.drawString("Waiting for GPS fix...", TFT_W / 2, TFT_CY);
            _tft.setTextDatum(TL_DATUM);
            return;
        }

        int16_t ex = (int16_t)((d.gpsLon - _homeLon) * 111320.0f *
                               cosf(_homeLat * 0.0174533f));
        int16_t ez = (int16_t)((d.gpsLat - _homeLat) * 111320.0f);

        int32_t maxR = 40;
        for (uint16_t i = 0; i < _trailCount; i++) {
            int32_t r = (int32_t)sqrtf((float)_trail[i].x * _trail[i].x +
                                       (float)_trail[i].y * _trail[i].y);
            if (r > maxR) maxR = r;
        }
        int32_t rCur = (int32_t)sqrtf((float)ex * ex + (float)ez * ez);
        if (rCur > maxR) maxR = rCur;

        uint16_t range = 50;
        const uint16_t ranges[] = {50, 100, 250, 500, 1000, 2000, 5000, 10000};
        for (uint8_t i = 0; i < sizeof(ranges) / sizeof(ranges[0]); i++) {
            if (maxR * 1.2f <= ranges[i]) { range = ranges[i]; break; }
            range = ranges[i];
        }

        const int16_t cx = TFT_W / 2;
        const int16_t cy = TFT_CY;
        float pxPerM = (float)(TFT_CY - 16) / (float)range;
        auto sx = [&](int16_t m) -> int16_t { return (int16_t)(cx + m * pxPerM); };
        auto sy = [&](int16_t m) -> int16_t { return (int16_t)(cy - m * pxPerM); };

        // rings
        for (int k = 1; k <= 3; k++) {
            int16_t rr = (int16_t)((range * k / 4.0f) * pxPerM);
            _tft.drawCircle(cx, cy, rr, _muted);
        }
        _tft.setTextFont(1);
        _tft.setTextColor(_muted, TFT_BLACK);
        _tft.setTextDatum(TR_DATUM);
        _tft.drawString("ring " + String(range) + "m", TFT_W - 6, 4);

        // trail
        for (uint16_t i = 1; i < _trailCount; i++) {
            _tft.drawLine(sx(_trail[i - 1].x), sy(_trail[i - 1].y),
                          sx(_trail[i].x), sy(_trail[i].y), _accent);
        }

        // home + aircraft
        _tft.fillCircle(cx, cy, 4, _tft.color565(34, 197, 94));
        _tft.setTextDatum(TC_DATUM);
        _tft.setTextColor(TFT_WHITE, TFT_BLACK);
        _tft.drawString("H", cx, cy - 14);

        int16_t ax = sx(ex), ay = sy(ez);
        float hdgRad = d.heading * 0.0174533f;
        int16_t nx = (int16_t)(sinf(hdgRad) * 9);
        int16_t ny = (int16_t)(-cosf(hdgRad) * 9);
        _tft.fillTriangle(ax + nx, ay + ny,
                          ax - ny / 2 - nx / 2, ay + nx / 2 - ny / 2,
                          ax + ny / 2 - nx / 2, ay - nx / 2 - ny / 2, _warn);

        // readouts
        _tft.setTextFont(1);
        _tft.setTextDatum(TL_DATUM);
        _tft.setTextColor(_text, TFT_BLACK);
        _tft.drawString(String(d.gpsLat, 5) + " " + String(d.gpsLon, 5), 4, 4);
        _tft.setTextColor(_muted, TFT_BLACK);
        _tft.drawString("D " + String(rCur) + "m  HDG " +
                        String((int)((d.heading < 0 ? d.heading + 360 : d.heading))) +
                        "  " + String((int)d.speedKmh) + "km/h", 4, 16);
        _tft.setTextDatum(TL_DATUM);
    }

    void updateTrail(const TelemetryData& d) {
        bool fix = d.gpsPosValid &&
                   (fabsf(d.gpsLat) > 0.00005f || fabsf(d.gpsLon) > 0.00005f);
        if (!_homeSet) {
            if (!fix) return;
            _homeSet = true;
            _homeLat = d.gpsLat;
            _homeLon = d.gpsLon;
            _trailCount = 0;
            _lastTrailMs = 0;
            return;
        }
        if (!fix) return;

        int16_t x = (int16_t)((d.gpsLon - _homeLon) * 111320.0f *
                              cosf(_homeLat * 0.0174533f));
        int16_t y = (int16_t)((d.gpsLat - _homeLat) * 111320.0f);

        unsigned long now = millis();
        float moved = sqrtf((float)(x - _trailLastX) * (x - _trailLastX) +
                            (float)(y - _trailLastY) * (y - _trailLastY));
        if (_trailCount > 0 && now - _lastTrailMs < 2000 && moved < 1.5f) return;

        if (_trailCount >= TRAIL_MAX) {
            memmove(&_trail[0], &_trail[1], sizeof(GpsTrailPt) * (TRAIL_MAX - 1));
            _trailCount = TRAIL_MAX - 1;
        }
        _trail[_trailCount].x = x;
        _trail[_trailCount].y = y;
        _trailCount++;
        _trailLastX = x;
        _trailLastY = y;
        _lastTrailMs = now;
    }

    // ------------------------------------------------------------------
    // Page 5: Diagnostics (also shows raw touch values for calibration)
    // ------------------------------------------------------------------
    void drawPageDiag(const TelemetryData& d, const SystemConfig& cfg,
                      const char* proto, const char* version) {
        _tft.fillRect(0, 0, TFT_W, TFT_H - TAB_H, TFT_BLACK);

        char lines[8][48];
        snprintf(lines[0], 48, "CYD %s  %s", version, proto);
        snprintf(lines[1], 48, "FPS %u  polls %u/s  CRC %lu",
                 (unsigned)d.packetsPerSec, (unsigned)d.pollsPerSec,
                 (unsigned long)d.crcErrors);
        snprintf(lines[2], 48, "PKT %lu  APP 0x%04X  PHY 0x%02X",
                 (unsigned long)d.validPackets, (unsigned)d.lastAppId,
                 (unsigned)d.lastPhysId);
        snprintf(lines[3], 48, "BUS %s  mode %s",
                 d.busMasterActive ? "BENCH MASTER" : (d.packetsPerSec ? "RX LINK" : "NO SIGNAL"),
                 (cfg.busMode == 0) ? "auto" : (cfg.busMode == 1) ? "receiver" : "bench");
        snprintf(lines[4], 48, "ATT %s  HDG %s  cells %uS",
                 attSourceLabel(d, millis()), headingSourceLabel(d, millis()),
                 (unsigned)d.detectedCells);
        snprintf(lines[5], 48, "WiFi 192.168.4.1  up %lus  heap %ukB",
                 millis() / 1000, (unsigned)(ESP.getFreeHeap() / 1024));
        snprintf(lines[6], 48, "touch raw %d,%d  mapped %d,%d",
                 _lastRawX, _lastRawY, _lastTouchX, _lastTouchY);

        _tft.setTextFont(2);
        _tft.setTextColor(TFT_WHITE, TFT_BLACK);
        _tft.setTextDatum(TL_DATUM);
        for (uint8_t i = 0; i < 7; i++) {
            _tft.drawString(lines[i], 6, 8 + i * 26);
        }
        _tft.setTextFont(1);
        _tft.setTextColor(_muted, TFT_BLACK);
        _tft.drawString("Hold BOOT >1.2s: colour / touch self-test", 6,
                        TFT_H - TAB_H - 16);
    }

    // ------------------------------------------------------------------
    // Self-test patterns (web oledTest or BOOT long-press)
    // ------------------------------------------------------------------
    void drawTest(uint8_t idx) {
        switch (idx % 7) {
            case 0: _tft.fillScreen(TFT_WHITE); break;
            case 1: _tft.fillScreen(TFT_BLACK); break;
            case 2: _tft.fillScreen(TFT_RED); break;
            case 3: _tft.fillScreen(TFT_GREEN); break;
            case 4: _tft.fillScreen(TFT_BLUE); break;
            case 5:
                for (int16_t y = 0; y < TFT_H; y += 12) {
                    for (int16_t x = 0; x < TFT_W; x += 12) {
                        if (((x / 12) + (y / 12)) & 1) {
                            _tft.fillRect(x, y, 12, 12, TFT_WHITE);
                        }
                    }
                }
                break;
            default:
                _tft.fillScreen(TFT_BLACK);
                for (int16_t x = 0; x < TFT_W; x += 40) {
                    _tft.drawFastVLine(x, 0, TFT_H, _muted);
                }
                for (int16_t y = 0; y < TFT_H; y += 40) {
                    _tft.drawFastHLine(0, y, TFT_W, _muted);
                }
                if (_lastTouchX >= 0) {
                    _tft.drawFastHLine(0, _lastTouchY, TFT_W, _accent);
                    _tft.drawFastVLine(_lastTouchX, 0, TFT_H, _accent);
                    _tft.drawCircle(_lastTouchX, _lastTouchY, 12, _accent);
                    _tft.setTextFont(2);
                    _tft.setTextColor(TFT_WHITE, TFT_BLACK);
                    _tft.setTextDatum(TL_DATUM);
                    _tft.drawString("raw " + String(_lastRawX) + "," +
                                    String(_lastRawY), 4, 4);
                }
                break;
        }
    }
};
