// =============================================================
//  DashboardServer.h  —  ESPAsyncWebServer REST API + captive portal
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
//
//  NOTE: Named DashboardServer (not WebServer) to avoid conflict
//  with the ESP32 Arduino framework's built-in <WebServer.h> class.
// =============================================================
#pragma once
#include <Arduino.h>
#include <ESPAsyncWebServer.h>
#include <ArduinoJson.h>
#include <functional>
#include "../config.h"
#include "../sensors/SoilMoisture.h"
#include "../sensors/WaterLevel.h"
#include "../sensors/FlowSensor.h"
#include "../irrigation/IrrigationController.h"
#include "../prediction/DryingModel.h"
#include "../storage/DataLogger.h"
#include "../network/WiFiManager.h"

// ─────────────────────────────────────────────────────────────
class DashboardServer {
public:
    DashboardServer();

    // Wire up all dependencies
    void setSoilSensor     (SoilMoisture*         s) { _soil  = s; }
    void setWaterLevel     (WaterLevel*            s) { _level = s; }
    void setFlowSensor     (FlowSensor*            s) { _flow  = s; }
    void setController     (IrrigationController*  c) { _ctrl  = c; }
    void setDryingModel    (DryingModel*           m) { _model = m; }
    void setDataLogger     (DataLogger*            l) { _log   = l; }
    void setWiFiManager    (WiFiManager*           w) { _wifi  = w; }
    void setUserConfig     (UserConfig*            u) { _cfg   = u; }
    void setActiveProfile  (const PlantProfile**   p) { _profile = p; }

    // Call once in setup()
    void begin();

    // No tick needed — ESPAsyncWebServer is interrupt/task driven

private:
    AsyncWebServer  _server;

    SoilMoisture*         _soil;
    WaterLevel*           _level;
    FlowSensor*           _flow;
    IrrigationController* _ctrl;
    DryingModel*          _model;
    DataLogger*           _log;
    WiFiManager*          _wifi;
    UserConfig*           _cfg;
    const PlantProfile**  _profile;

    // ── Route registration ────────────────────────────────────
    void _registerStaticFiles();
    void _registerAPIRoutes();
    void _registerCaptivePortal();

    // ── Endpoint handlers ─────────────────────────────────────
    void _handleStatus        (AsyncWebServerRequest* req);
    void _handleHistory       (AsyncWebServerRequest* req);
    void _handleConfig        (AsyncWebServerRequest* req);
    void _handlePostConfig    (AsyncWebServerRequest* req, JsonDocument& body);
    void _handlePostProfile   (AsyncWebServerRequest* req, JsonDocument& body);
    void _handlePostWater     (AsyncWebServerRequest* req);
    void _handlePostStop      (AsyncWebServerRequest* req);
    void _handlePostCalibrate (AsyncWebServerRequest* req, JsonDocument& body);
    void _handlePostWifi      (AsyncWebServerRequest* req, JsonDocument& body);
    void _handleResetHistory  (AsyncWebServerRequest* req);

    // ── Helpers ───────────────────────────────────────────────
    void _sendJson(AsyncWebServerRequest* req, int code, const String& json);
    void _sendError(AsyncWebServerRequest* req, int code, const char* msg);

    // Build a JSON body handler closure (parses incoming body then dispatches)
    static void _bodyHandler(
        AsyncWebServerRequest* req,
        uint8_t* data, size_t len, size_t index, size_t total,
        std::function<void(AsyncWebServerRequest*, JsonDocument&)> handler);
};
