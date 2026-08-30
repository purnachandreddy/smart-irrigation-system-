// =============================================================
//  DataLogger.cpp  —  LittleFS log + NVS configuration storage
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#include "DataLogger.h"
#include <LittleFS.h>
#include <Preferences.h>
#include <time.h>

// Rate-limit periodic flash writes: at most once per minute
static constexpr uint32_t WRITE_INTERVAL_MS = 60000UL;

// ─────────────────────────────────────────────────────────────
DataLogger::DataLogger()
    : _ringHead(0), _ringCount(0), _lastWriteMs(0)
{
    memset(_ring, 0, sizeof(_ring));
}

bool DataLogger::begin() {
    if (!LittleFS.begin(true)) {   // true = format if mount fails
        LOG("DataLogger: LittleFS mount FAILED — formatting...");
        return false;
    }
    LOG("DataLogger: LittleFS mounted. Used:%zu Total:%zu",
        getUsedBytes(), getTotalBytes());
    return true;
}

// ─────────────────────────────────────────────────────────────
void DataLogger::logEvent(LogEventType type,
                           float moisture,
                           float tankLevel,
                           bool  pumpState,
                           float waterAmountMl,
                           const char* notes) {
    LogEntry entry;
    entry.timestampMs   = millis();
    entry.type          = type;
    entry.moisture      = moisture;
    entry.tankLevel     = tankLevel;
    entry.pumpState     = pumpState;
    entry.waterAmountMl = waterAmountMl;
    strncpy(entry.notes, notes ? notes : "", sizeof(entry.notes) - 1);
    entry.notes[sizeof(entry.notes) - 1] = '\0';

    // Add to in-memory ring buffer
    _ring[_ringHead] = entry;
    _ringHead = (_ringHead + 1) % LOG_MAX_ENTRIES_MEMORY;
    if (_ringCount < LOG_MAX_ENTRIES_MEMORY) _ringCount++;

    // Immediately write important events; defer routine ones
    bool urgent = (type == LogEventType::WATERING_START  ||
                   type == LogEventType::WATERING_DONE   ||
                   type == LogEventType::WATERING_FAILED ||
                   type == LogEventType::FAULT_DETECTED  ||
                   type == LogEventType::SYSTEM_BOOT     ||
                   type == LogEventType::MANUAL_WATER);

    if (urgent) {
        _appendToFile(entry);
    }

    LOG("DataLogger: event type=%d moisture=%.1f%% notes=%s",
        (int)type, moisture, entry.notes);
}

// ─────────────────────────────────────────────────────────────
void DataLogger::periodicWrite(uint32_t nowMs) {
    if ((nowMs - _lastWriteMs) < WRITE_INTERVAL_MS) return;
    _lastWriteMs = nowMs;

    // Write the most recent ring entry to flash (if any)
    if (_ringCount == 0) return;

    uint8_t latest = (_ringHead == 0) ? (LOG_MAX_ENTRIES_MEMORY - 1) : (_ringHead - 1);
    _appendToFile(_ring[latest]);
    _rotateFileIfNeeded();
}

// ─────────────────────────────────────────────────────────────
String DataLogger::getHistoryJson(uint8_t n) const {
    // Build JSON array from in-memory ring (newest-first)
    JsonDocument doc;
    JsonArray arr = doc.to<JsonArray>();

    uint8_t count = min((uint16_t)n, _ringCount);
    for (uint8_t i = 0; i < count; i++) {
        // Walk backwards from head
        int idx = ((int)_ringHead - 1 - i + LOG_MAX_ENTRIES_MEMORY) % LOG_MAX_ENTRIES_MEMORY;
        const LogEntry& e = _ring[idx];

        JsonObject obj = arr.add<JsonObject>();
        obj["ts"]       = e.timestampMs;
        obj["type"]     = (int)e.type;
        obj["moisture"] = (int)e.moisture;
        obj["tank"]     = (int)e.tankLevel;
        obj["pump"]     = e.pumpState;
        obj["water_ml"] = e.waterAmountMl;
        obj["notes"]    = e.notes;
    }

    String out;
    serializeJson(doc, out);
    return out;
}

// ─────────────────────────────────────────────────────────────
void DataLogger::clearHistory() {
    if (LittleFS.exists(LOG_FILE_PATH)) {
        LittleFS.remove(LOG_FILE_PATH);
    }
    memset(_ring, 0, sizeof(_ring));
    _ringHead  = 0;
    _ringCount = 0;
    LOG("DataLogger: history cleared");
}

// ─────────────────────────────────────────────────────────────
//  Configuration (NVS)
// ─────────────────────────────────────────────────────────────
bool DataLogger::loadConfig(UserConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, true)) {  // read-only
        LOG("DataLogger: NVS open failed — using defaults");
        return false;
    }

    strncpy(cfg.wifiSSID,     prefs.getString("wifi_ssid",  "").c_str(), sizeof(cfg.wifiSSID) - 1);
    strncpy(cfg.wifiPassword, prefs.getString("wifi_pass",  "").c_str(), sizeof(cfg.wifiPassword) - 1);

    cfg.soilDryValue  = prefs.getInt("soil_dry",  DEFAULT_SOIL_DRY_VALUE);
    cfg.soilWetValue  = prefs.getInt("soil_wet",  DEFAULT_SOIL_WET_VALUE);

    strncpy(cfg.activeProfile, prefs.getString("profile", "normal").c_str(),
            sizeof(cfg.activeProfile) - 1);

    cfg.customMinMoisture         = prefs.getFloat("cust_min",    35.0f);
    cfg.customTargetMoisture      = prefs.getFloat("cust_tgt",    55.0f);
    cfg.customMaxMoisture         = prefs.getFloat("cust_max",    70.0f);
    cfg.customCriticalDryness     = prefs.getFloat("cust_crit",   25.0f);
    cfg.customPulseDurationMs     = prefs.getUInt ("cust_pulse",  4000);
    cfg.customStabilizationDelayMs= prefs.getUInt ("cust_stab",   30000);
    cfg.customWateringCooldownMs  = prefs.getUInt ("cust_cool",   1800000);
    cfg.customMaxDailyWaterMl     = prefs.getUShort("cust_daily", 600);
    cfg.customMaxPulsesPerSession = prefs.getUChar("cust_pulses", 6);

    cfg.adaptivePredictionEnabled = prefs.getBool("adaptive",     true);
    cfg.autoWateringEnabled       = prefs.getBool("auto_water",   true);
    cfg.gmtOffsetSec              = prefs.getInt("gmt_offset",    NTP_GMT_OFFSET_SEC);

    prefs.end();
    LOG("DataLogger: config loaded — profile=%s", cfg.activeProfile);
    return true;
}

bool DataLogger::saveConfig(const UserConfig& cfg) {
    Preferences prefs;
    if (!prefs.begin(NVS_NAMESPACE, false)) {  // read-write
        LOG("DataLogger: NVS open failed — cannot save");
        return false;
    }

    prefs.putString("wifi_ssid",   cfg.wifiSSID);
    prefs.putString("wifi_pass",   cfg.wifiPassword);
    prefs.putInt   ("soil_dry",    cfg.soilDryValue);
    prefs.putInt   ("soil_wet",    cfg.soilWetValue);
    prefs.putString("profile",     cfg.activeProfile);

    prefs.putFloat ("cust_min",    cfg.customMinMoisture);
    prefs.putFloat ("cust_tgt",    cfg.customTargetMoisture);
    prefs.putFloat ("cust_max",    cfg.customMaxMoisture);
    prefs.putFloat ("cust_crit",   cfg.customCriticalDryness);
    prefs.putUInt  ("cust_pulse",  cfg.customPulseDurationMs);
    prefs.putUInt  ("cust_stab",   cfg.customStabilizationDelayMs);
    prefs.putUInt  ("cust_cool",   cfg.customWateringCooldownMs);
    prefs.putUShort("cust_daily",  cfg.customMaxDailyWaterMl);
    prefs.putUChar ("cust_pulses", cfg.customMaxPulsesPerSession);
    prefs.putBool  ("adaptive",    cfg.adaptivePredictionEnabled);
    prefs.putBool  ("auto_water",  cfg.autoWateringEnabled);
    prefs.putInt   ("gmt_offset",  cfg.gmtOffsetSec);

    prefs.end();
    LOG("DataLogger: config saved — profile=%s", cfg.activeProfile);
    return true;
}

// ─────────────────────────────────────────────────────────────
const PlantProfile* DataLogger::resolveActiveProfile(const UserConfig& cfg) {
    if (strcmp(cfg.activeProfile, "cactus") == 0)           return &PROFILE_CACTUS;
    if (strcmp(cfg.activeProfile, "moisture_loving") == 0)  return &PROFILE_MOISTURE_LOVING;
    if (strcmp(cfg.activeProfile, "custom") == 0)           return &PROFILE_CUSTOM;
    return &PROFILE_NORMAL;  // default
}

// ─────────────────────────────────────────────────────────────
size_t DataLogger::getUsedBytes()  const { return LittleFS.usedBytes();  }
size_t DataLogger::getTotalBytes() const { return LittleFS.totalBytes(); }

// ─────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────
void DataLogger::_appendToFile(const LogEntry& entry) {
    File f = LittleFS.open(LOG_FILE_PATH, "a");
    if (!f) {
        LOG("DataLogger: cannot open log file for append");
        return;
    }

    // Write as compact JSON line (JSONL format)
    JsonDocument doc;
    doc["ts"]      = entry.timestampMs;
    doc["type"]    = (int)entry.type;
    doc["m"]       = (int)entry.moisture;
    doc["t"]       = (int)entry.tankLevel;
    doc["p"]       = entry.pumpState ? 1 : 0;
    doc["w"]       = entry.waterAmountMl;
    doc["n"]       = entry.notes;

    serializeJson(doc, f);
    f.print('\n');
    f.close();
}

void DataLogger::_rotateFileIfNeeded() {
    if (!LittleFS.exists(LOG_FILE_PATH)) return;

    File f = LittleFS.open(LOG_FILE_PATH, "r");
    if (!f) return;
    size_t size = f.size();
    f.close();

    if (size < LOG_MAX_FILE_SIZE_BYTES) return;

    // Simple rotation: delete old file (ring buffer in RAM preserves recent data)
    LittleFS.remove(LOG_FILE_PATH);
    LOG("DataLogger: log file rotated (was %zu bytes)", size);
}
