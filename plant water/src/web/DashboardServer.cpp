// =============================================================
//  DashboardServer.cpp  —  ESPAsyncWebServer REST API + captive portal
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#include "DashboardServer.h"
#include <LittleFS.h>
#include <functional>

// ─────────────────────────────────────────────────────────────
DashboardServer::DashboardServer()
    : _server(WEB_SERVER_PORT),
      _soil(nullptr), _level(nullptr), _flow(nullptr),
      _ctrl(nullptr), _model(nullptr), _log(nullptr),
      _wifi(nullptr), _cfg(nullptr), _profile(nullptr)
{}

// ─────────────────────────────────────────────────────────────
void DashboardServer::begin() {
    _registerStaticFiles();
    _registerAPIRoutes();
    _registerCaptivePortal();

    // CORS for development convenience
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Origin",  "*");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Methods", "GET,POST,OPTIONS");
    DefaultHeaders::Instance().addHeader("Access-Control-Allow-Headers", "Content-Type");

    _server.begin();
    LOG("WebServer: listening on port %d", WEB_SERVER_PORT);
}

// ─────────────────────────────────────────────────────────────
//  Static file serving from LittleFS
// ─────────────────────────────────────────────────────────────
void DashboardServer::_registerStaticFiles() {
    // Serve index.html at root
    _server.on("/", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/index.html", "text/html");
    });
    _server.on("/style.css", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/style.css", "text/css");
    });
    _server.on("/app.js", HTTP_GET, [](AsyncWebServerRequest* req) {
        req->send(LittleFS, "/app.js", "application/javascript");
    });

    // OPTIONS pre-flight
    _server.on("/*", HTTP_OPTIONS, [](AsyncWebServerRequest* req) {
        req->send(204);
    });
}

// ─────────────────────────────────────────────────────────────
//  Captive portal: redirect all unknown requests to /
// ─────────────────────────────────────────────────────────────
void DashboardServer::_registerCaptivePortal() {
    _server.onNotFound([](AsyncWebServerRequest* req) {
        // If this is a connectivity check, redirect to setup page
        if (req->host() != WiFi.softAPIP().toString()) {
            req->redirect("http://" + WiFi.softAPIP().toString() + "/");
            return;
        }
        req->send(404, "text/plain", "Not found");
    });
}

// ─────────────────────────────────────────────────────────────
//  REST API routes
// ─────────────────────────────────────────────────────────────
void DashboardServer::_registerAPIRoutes() {
    // GET /api/status  — full system snapshot
    _server.on("/api/status", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _handleStatus(req);
    });

    // GET /api/history?n=50
    _server.on("/api/history", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _handleHistory(req);
    });

    // GET /api/config
    _server.on("/api/config", HTTP_GET, [this](AsyncWebServerRequest* req) {
        _handleConfig(req);
    });

    // POST /api/config  — update parameters
    _server.on("/api/config", HTTP_POST,
        [](AsyncWebServerRequest*) {},  // send: handled in body
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            _bodyHandler(req, data, len, index, total,
                [this](AsyncWebServerRequest* r, JsonDocument& body) {
                    _handlePostConfig(r, body);
                });
        });

    // POST /api/profile  — change plant profile
    _server.on("/api/profile", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            _bodyHandler(req, data, len, index, total,
                [this](AsyncWebServerRequest* r, JsonDocument& body) {
                    _handlePostProfile(r, body);
                });
        });

    // POST /api/water  — manual watering trigger
    _server.on("/api/water", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _handlePostWater(req);
    });

    // POST /api/stop  — emergency pump stop
    _server.on("/api/stop", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _handlePostStop(req);
    });

    // POST /api/calibrate  — set dry/wet ADC values
    _server.on("/api/calibrate", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            _bodyHandler(req, data, len, index, total,
                [this](AsyncWebServerRequest* r, JsonDocument& body) {
                    _handlePostCalibrate(r, body);
                });
        });

    // POST /api/wifi  — save Wi-Fi credentials (captive portal form)
    _server.on("/api/wifi", HTTP_POST,
        [](AsyncWebServerRequest*) {},
        nullptr,
        [this](AsyncWebServerRequest* req, uint8_t* data, size_t len, size_t index, size_t total) {
            _bodyHandler(req, data, len, index, total,
                [this](AsyncWebServerRequest* r, JsonDocument& body) {
                    _handlePostWifi(r, body);
                });
        });

    // POST /api/reset-history
    _server.on("/api/reset-history", HTTP_POST, [this](AsyncWebServerRequest* req) {
        _handleResetHistory(req);
    });
}

// ─────────────────────────────────────────────────────────────
//  GET /api/status
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handleStatus(AsyncWebServerRequest* req) {
    JsonDocument doc;

    // ── System ────────────────────────────────────────────────
    doc["fw_version"]    = FW_VERSION_STR;
    doc["uptime_ms"]     = millis();

    // ── Soil ──────────────────────────────────────────────────
    if (_soil) {
        doc["soil"]["pct"]        = (int)(_soil->getPercentage());
        doc["soil"]["raw"]        = _soil->getRawADC();
        doc["soil"]["state"]      = _soil->getStateStr();
        doc["soil"]["valid"]      = _soil->isValid();
    }

    // ── Tank ──────────────────────────────────────────────────
    if (_level) {
        doc["tank"]["pct"]        = (int)(_level->getPercentage());
        doc["tank"]["state"]      = _level->getStateStr();
        doc["tank"]["safe"]       = _level->isSafeToWater();
    }

    // ── Flow (optional) ───────────────────────────────────────
    if (_flow) {
        doc["flow"]["rate_ml_min"]= (int)(_flow->getFlowRateMlPerMin());
        doc["flow"]["session_ml"] = (int)(_flow->getSessionVolumeMl());
        doc["flow"]["daily_ml"]   = (int)(_flow->getDailyVolumeMl());
    }

    // ── Irrigation controller ─────────────────────────────────
    if (_ctrl) {
        doc["ctrl"]["state"]       = _ctrl->getStateStr();
        doc["ctrl"]["plant_status"]= _ctrl->getPlantStatusStr();
        doc["ctrl"]["emoji"]       = _ctrl->getStatusEmoji();
        doc["ctrl"]["pump_on"]     = _ctrl->isPumpOn();
        doc["ctrl"]["auto_enabled"]= _ctrl->isAutoEnabled();
        doc["ctrl"]["adaptive"]    = _ctrl->isAdaptiveEnabled();
        doc["ctrl"]["daily_ml"]    = (int)(_ctrl->getDailyWaterMl());
        doc["ctrl"]["last_water_ms"]= _ctrl->getLastWateringMs();
        doc["ctrl"]["last_error"]  = _ctrl->getLastError();
        doc["ctrl"]["pulse_num"]   = _ctrl->getCurrentPulse();

        float minUntil = _ctrl->getMinutesUntilWater();
        if (!isnan(minUntil)) {
            doc["ctrl"]["mins_until_water"] = (int)minUntil;
        } else {
            doc["ctrl"]["mins_until_water"] = nullptr;
        }

        // Last session
        const WateringSession& sess = _ctrl->getLastSession();
        doc["ctrl"]["last_session"]["moisture_before"] = (int)sess.moistureBefore;
        doc["ctrl"]["last_session"]["moisture_after"]  = (int)sess.moistureAfter;
        doc["ctrl"]["last_session"]["pulses"]          = sess.pulseCount;
        doc["ctrl"]["last_session"]["water_ml"]        = (int)sess.totalWaterMl;
        doc["ctrl"]["last_session"]["target_reached"]  = sess.targetReached;
    }

    // ── Prediction ────────────────────────────────────────────
    if (_model) {
        float rate = _model->getDryingRatePctPerHour();
        if (!isnan(rate)) {
            doc["prediction"]["rate_pct_hr"]  = rate;
        } else {
            doc["prediction"]["rate_pct_hr"]  = nullptr;
        }
        doc["prediction"]["data_points"]      = _model->getCount();
        doc["prediction"]["status"]           = _model->getStatusStr();
        doc["prediction"]["sufficient"]       = _model->isDataSufficient();

        if (_ctrl && _soil) {
            float mins = _ctrl->getMinutesUntilWater();
            if (isnan(mins)) {
                doc["prediction"]["mins_until_critical"] = nullptr;
            } else {
                doc["prediction"]["mins_until_critical"] = (int)mins;
            }
        }
    }

    // ── Active profile ────────────────────────────────────────
    if (_profile && *_profile) {
        const PlantProfile* p = *_profile;
        doc["profile"]["id"]           = p->id;
        doc["profile"]["name"]         = p->name;
        doc["profile"]["emoji"]        = p->emoji;
        doc["profile"]["min_moisture"] = (int)p->minMoisture;
        doc["profile"]["target"]       = (int)p->targetMoisture;
        doc["profile"]["max_moisture"] = (int)p->maxMoisture;
        doc["profile"]["critical"]     = (int)p->criticalDryness;
    }

    // ── Wi-Fi ─────────────────────────────────────────────────
    if (_wifi) {
        doc["wifi"]["mode"]      = _wifi->getModeStr();
        doc["wifi"]["connected"] = _wifi->isConnected();
        doc["wifi"]["ssid"]      = _wifi->getSSID();
        doc["wifi"]["ip"]        = _wifi->getLocalIP();
        doc["wifi"]["rssi"]      = _wifi->getRSSI();
        doc["wifi"]["ntp_synced"]= _wifi->isTimeSynced();
    }

    // ── Time ──────────────────────────────────────────────────
    struct tm ti;
    if (getLocalTime(&ti, 0)) {
        char buf[32];
        strftime(buf, sizeof(buf), "%H:%M:%S", &ti);
        doc["time"]["hms"] = buf;
        strftime(buf, sizeof(buf), "%Y-%m-%d", &ti);
        doc["time"]["date"] = buf;
    }

    String out;
    serializeJson(doc, out);
    _sendJson(req, 200, out);
}

// ─────────────────────────────────────────────────────────────
//  GET /api/history
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handleHistory(AsyncWebServerRequest* req) {
    uint8_t n = 50;
    if (req->hasParam("n")) {
        n = (uint8_t)req->getParam("n")->value().toInt();
        n = constrain(n, 1, 100);
    }
    if (_log) {
        _sendJson(req, 200, _log->getHistoryJson(n));
    } else {
        _sendJson(req, 200, "[]");
    }
}

// ─────────────────────────────────────────────────────────────
//  GET /api/config
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handleConfig(AsyncWebServerRequest* req) {
    if (!_cfg) { _sendError(req, 500, "Config unavailable"); return; }

    JsonDocument doc;
    doc["soil_dry"]            = _cfg->soilDryValue;
    doc["soil_wet"]            = _cfg->soilWetValue;
    doc["active_profile"]      = _cfg->activeProfile;
    doc["adaptive_enabled"]    = _cfg->adaptivePredictionEnabled;
    doc["auto_watering"]       = _cfg->autoWateringEnabled;
    doc["gmt_offset"]          = _cfg->gmtOffsetSec;
    doc["custom"]["min"]       = _cfg->customMinMoisture;
    doc["custom"]["target"]    = _cfg->customTargetMoisture;
    doc["custom"]["max"]       = _cfg->customMaxMoisture;
    doc["custom"]["critical"]  = _cfg->customCriticalDryness;
    doc["custom"]["pulse_ms"]  = _cfg->customPulseDurationMs;
    doc["custom"]["stab_ms"]   = _cfg->customStabilizationDelayMs;
    doc["custom"]["cool_ms"]   = _cfg->customWateringCooldownMs;
    doc["custom"]["daily_ml"]  = _cfg->customMaxDailyWaterMl;
    doc["custom"]["max_pulses"]= _cfg->customMaxPulsesPerSession;

    String out;
    serializeJson(doc, out);
    _sendJson(req, 200, out);
}

// ─────────────────────────────────────────────────────────────
//  POST /api/config
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handlePostConfig(AsyncWebServerRequest* req, JsonDocument& body) {
    if (!_cfg || !_log) { _sendError(req, 500, "Config unavailable"); return; }

    // ArduinoJson v7: containsKey() removed — use !doc["key"].isNull() instead
    if (!body["adaptive_enabled"].isNull())
        _cfg->adaptivePredictionEnabled = body["adaptive_enabled"].as<bool>();
    if (!body["auto_watering"].isNull())
        _cfg->autoWateringEnabled = body["auto_watering"].as<bool>();
    if (!body["gmt_offset"].isNull())
        _cfg->gmtOffsetSec = body["gmt_offset"].as<int32_t>();

    // Custom profile overrides
    if (!body["custom"].isNull()) {
        JsonObject c = body["custom"].as<JsonObject>();
        if (!c["min"].isNull())        _cfg->customMinMoisture         = c["min"].as<float>();
        if (!c["target"].isNull())     _cfg->customTargetMoisture      = c["target"].as<float>();
        if (!c["max"].isNull())        _cfg->customMaxMoisture         = c["max"].as<float>();
        if (!c["critical"].isNull())   _cfg->customCriticalDryness     = c["critical"].as<float>();
        if (!c["pulse_ms"].isNull())   _cfg->customPulseDurationMs     = c["pulse_ms"].as<uint32_t>();
        if (!c["stab_ms"].isNull())    _cfg->customStabilizationDelayMs= c["stab_ms"].as<uint32_t>();
        if (!c["cool_ms"].isNull())    _cfg->customWateringCooldownMs  = c["cool_ms"].as<uint32_t>();
        if (!c["daily_ml"].isNull())   _cfg->customMaxDailyWaterMl     = c["daily_ml"].as<uint16_t>();
        if (!c["max_pulses"].isNull()) _cfg->customMaxPulsesPerSession  = c["max_pulses"].as<uint8_t>();
    }

    // Apply runtime flags to controller
    if (_ctrl) {
        _ctrl->enableAutoWatering(_cfg->autoWateringEnabled);
        _ctrl->enableAdaptive(_cfg->adaptivePredictionEnabled);
    }

    _log->saveConfig(*_cfg);
    _log->logEvent(LogEventType::CONFIG_CHANGED, 0, 0, false, 0, "Config updated via dashboard");
    _sendJson(req, 200, "{\"ok\":true}");
}

// ─────────────────────────────────────────────────────────────
//  POST /api/profile
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handlePostProfile(AsyncWebServerRequest* req, JsonDocument& body) {
    if (!_cfg || !_ctrl || !_profile) { _sendError(req, 500, "Controller unavailable"); return; }

    String profileId = body["profile"].as<String>();
    const PlantProfile* newProfile = nullptr;

    for (uint8_t i = 0; i < NUM_BUILT_IN_PROFILES; i++) {
        if (String(BUILT_IN_PROFILES[i]->id) == profileId) {
            newProfile = BUILT_IN_PROFILES[i];
            break;
        }
    }
    if (profileId == "custom") newProfile = &PROFILE_CUSTOM;

    if (!newProfile) {
        _sendError(req, 400, "Unknown profile id");
        return;
    }

    strncpy(_cfg->activeProfile, profileId.c_str(), sizeof(_cfg->activeProfile) - 1);
    *_profile = newProfile;
    _ctrl->setProfile(newProfile);
    _log->saveConfig(*_cfg);

    char note[48];
    snprintf(note, sizeof(note), "Profile -> %s", newProfile->name);
    _log->logEvent(LogEventType::PROFILE_CHANGED, 0, 0, false, 0, note);

    _sendJson(req, 200, "{\"ok\":true}");
}

// ─────────────────────────────────────────────────────────────
//  POST /api/water
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handlePostWater(AsyncWebServerRequest* req) {
    if (!_ctrl) { _sendError(req, 500, "Controller unavailable"); return; }
    _ctrl->startManualWatering();
    _sendJson(req, 200, "{\"ok\":true,\"msg\":\"Manual watering initiated\"}");
}

// ─────────────────────────────────────────────────────────────
//  POST /api/stop
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handlePostStop(AsyncWebServerRequest* req) {
    if (!_ctrl) { _sendError(req, 500, "Controller unavailable"); return; }
    _ctrl->emergencyStop("user request via dashboard");
    _sendJson(req, 200, "{\"ok\":true,\"msg\":\"Pump stopped\"}");
}

// ─────────────────────────────────────────────────────────────
//  POST /api/calibrate
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handlePostCalibrate(AsyncWebServerRequest* req, JsonDocument& body) {
    if (!_soil || !_cfg || !_log) { _sendError(req, 500, "Sensor unavailable"); return; }

    String action = body["action"].as<String>();
    JsonDocument resp;

    if (action == "sample") {
        // Sample the current raw ADC value for the user to record
        int raw = _soil->sampleForCalibration();
        resp["raw"] = raw;
        resp["pct"] = _soil->getPercentage();
        String out; serializeJson(resp, out);
        _sendJson(req, 200, out);
        return;
    }

    if (action == "set") {
        int dry = body["dry"].as<int>();
        int wet = body["wet"].as<int>();
        if (dry <= wet) {
            _sendError(req, 400, "dry value must be greater than wet value");
            return;
        }
        _cfg->soilDryValue = dry;
        _cfg->soilWetValue = wet;
        _soil->setCalibration(dry, wet);
        _log->saveConfig(*_cfg);
        _log->logEvent(LogEventType::CALIBRATION, 0, 0, false, 0, "Soil calibration updated");
        _sendJson(req, 200, "{\"ok\":true}");
        return;
    }

    _sendError(req, 400, "Unknown calibrate action");
}

// ─────────────────────────────────────────────────────────────
//  POST /api/wifi  — captive portal credential submission
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handlePostWifi(AsyncWebServerRequest* req, JsonDocument& body) {
    if (!_wifi || !_cfg || !_log) { _sendError(req, 500, "WiFi manager unavailable"); return; }

    String ssid = body["ssid"].as<String>();
    String pass = body["password"].as<String>();

    if (ssid.isEmpty()) {
        _sendError(req, 400, "SSID is required");
        return;
    }

    strncpy(_cfg->wifiSSID,     ssid.c_str(), sizeof(_cfg->wifiSSID) - 1);
    strncpy(_cfg->wifiPassword, pass.c_str(), sizeof(_cfg->wifiPassword) - 1);
    _log->saveConfig(*_cfg);

    _wifi->setCredentials(ssid.c_str(), pass.c_str());

    _sendJson(req, 200, "{\"ok\":true,\"msg\":\"Credentials saved — attempting connection\"}");
}

// ─────────────────────────────────────────────────────────────
//  POST /api/reset-history
// ─────────────────────────────────────────────────────────────
void DashboardServer::_handleResetHistory(AsyncWebServerRequest* req) {
    if (!_log || !_model) { _sendError(req, 500, "Logger unavailable"); return; }
    _log->clearHistory();
    _model->reset();
    _sendJson(req, 200, "{\"ok\":true}");
}

// ─────────────────────────────────────────────────────────────
//  Helpers
// ─────────────────────────────────────────────────────────────
void DashboardServer::_sendJson(AsyncWebServerRequest* req, int code, const String& json) {
    AsyncWebServerResponse* resp = req->beginResponse(code, "application/json", json);
    req->send(resp);
}

void DashboardServer::_sendError(AsyncWebServerRequest* req, int code, const char* msg) {
    String body = String("{\"error\":\"") + msg + "\"}";
    _sendJson(req, code, body);
}

// Generic body accumulator — buffers the full POST body before parsing JSON
void DashboardServer::_bodyHandler(
    AsyncWebServerRequest* req,
    uint8_t* data, size_t len, size_t index, size_t total,
    std::function<void(AsyncWebServerRequest*, JsonDocument&)> handler)
{
    static uint8_t bodyBuf[1024];
    static size_t  bodyLen = 0;

    if (index == 0) bodyLen = 0;

    size_t toCopy = min(len, sizeof(bodyBuf) - bodyLen);
    memcpy(bodyBuf + bodyLen, data, toCopy);
    bodyLen += toCopy;

    if (index + len >= total) {
        JsonDocument doc;
        DeserializationError err = deserializeJson(doc, bodyBuf, bodyLen);
        bodyLen = 0;

        if (err) {
            AsyncWebServerResponse* resp = req->beginResponse(
                400, "application/json", "{\"error\":\"Invalid JSON\"}");
            req->send(resp);
            return;
        }
        handler(req, doc);
    }
}
