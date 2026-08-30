// =============================================================
//  DataLogger.h  —  LittleFS log + NVS configuration storage
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#pragma once
#include <Arduino.h>
#include <ArduinoJson.h>
#include "../config.h"
#include "../irrigation/PlantProfiles.h"

// ─────────────────────────────────────────────────────────────
//  Log event types
// ─────────────────────────────────────────────────────────────
enum class LogEventType : uint8_t {
    SENSOR_READ     = 0,
    WATERING_START  = 1,
    WATERING_PULSE  = 2,
    WATERING_DONE   = 3,
    WATERING_FAILED = 4,
    FAULT_DETECTED  = 5,
    FAULT_CLEARED   = 6,
    PROFILE_CHANGED = 7,
    CONFIG_CHANGED  = 8,
    SYSTEM_BOOT     = 9,
    MANUAL_WATER    = 10,
    CALIBRATION     = 11,
};

// ─────────────────────────────────────────────────────────────
//  Single log entry (kept small for RAM efficiency)
// ─────────────────────────────────────────────────────────────
struct LogEntry {
    uint32_t    timestampMs;
    LogEventType type;
    float       moisture;      // % at event time
    float       tankLevel;     // % at event time
    float       waterAmountMl; // mL dispensed (if applicable)
    bool        pumpState;
    char        notes[48];     // Short message
};

// ─────────────────────────────────────────────────────────────
//  Persistent user configuration (stored in NVS)
// ─────────────────────────────────────────────────────────────
struct UserConfig {
    char     wifiSSID[64];
    char     wifiPassword[64];

    // Soil calibration
    int      soilDryValue;
    int      soilWetValue;

    // Active profile ('cactus', 'normal', 'moisture_loving', 'custom')
    char     activeProfile[24];

    // Custom profile overrides (writable from dashboard)
    float    customMinMoisture;
    float    customTargetMoisture;
    float    customMaxMoisture;
    float    customCriticalDryness;
    uint32_t customPulseDurationMs;
    uint32_t customStabilizationDelayMs;
    uint32_t customWateringCooldownMs;
    uint16_t customMaxDailyWaterMl;
    uint8_t  customMaxPulsesPerSession;

    // Feature toggles
    bool     adaptivePredictionEnabled;
    bool     autoWateringEnabled;

    // Timezone offset in seconds (for NTP)
    int32_t  gmtOffsetSec;
};

// ─────────────────────────────────────────────────────────────
class DataLogger {
public:
    DataLogger();

    // Call in setup()
    bool begin();

    // ── Event logging ─────────────────────────────────────────

    void logEvent(LogEventType type,
                  float moisture,
                  float tankLevel,
                  bool  pumpState,
                  float waterAmountMl = 0.0f,
                  const char* notes   = "");

    // Write current state record to LittleFS periodically
    // (not on every tick — rate limited internally)
    void periodicWrite(uint32_t nowMs);

    // Returns last N entries as a JSON array string
    // Caller must free the returned string
    String  getHistoryJson(uint8_t n = 50) const;

    // Clear the log file
    void    clearHistory();

    // ── Configuration ─────────────────────────────────────────

    bool        loadConfig(UserConfig& cfg);
    bool        saveConfig(const UserConfig& cfg);

    // Convenience: load and apply active plant profile from config
    const PlantProfile* resolveActiveProfile(const UserConfig& cfg);

    // LittleFS filesystem info
    size_t      getUsedBytes()  const;
    size_t      getTotalBytes() const;

private:
    // In-memory ring buffer for recent entries (for fast API response)
    LogEntry    _ring[LOG_MAX_ENTRIES_MEMORY];
    uint8_t     _ringHead;
    uint16_t    _ringCount;

    uint32_t    _lastWriteMs;

    void        _appendToFile(const LogEntry& entry);
    void        _rotateFileIfNeeded();
};
