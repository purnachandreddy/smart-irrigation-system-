// =============================================================
//  IrrigationController.h  —  Core state machine & pump control
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#pragma once
#include <Arduino.h>
#include "../config.h"
#include "../irrigation/PlantProfiles.h"
#include "../sensors/SoilMoisture.h"
#include "../sensors/WaterLevel.h"
#include "../sensors/FlowSensor.h"
#include "../prediction/DryingModel.h"
#include "../storage/DataLogger.h"

// ─────────────────────────────────────────────────────────────
//  Irrigation states
// ─────────────────────────────────────────────────────────────
enum class IrrigationState : uint8_t {
    STARTUP,
    CALIBRATING,
    MONITORING,       // Normal operation — soil healthy
    DRYING,           // Soil drying but not yet urgent
    WATER_SOON,       // Prediction: will need water within N minutes
    WATERING,         // Active pump pulse
    STABILIZING,      // Waiting for water to distribute after pulse
    VERIFYING,        // Reading moisture post-stabilization
    WATERING_COMPLETE,// Target reached — just finished
    LOW_WATER,        // Tank low — restricted operation
    SENSOR_ERROR,     // Soil sensor unreliable
    PUMP_ERROR,       // Pump ran but no moisture rise / no flow
    CRITICAL_ERROR,   // Multiple faults — safe shutdown
};

// ─────────────────────────────────────────────────────────────
//  Plant status (what to display on dashboard)
// ─────────────────────────────────────────────────────────────
enum class PlantStatus : uint8_t {
    HEALTHY,
    DRYING,
    WATER_SOON,
    NEEDS_WATER,
    CRITICAL,
    ERROR,
};

// ─────────────────────────────────────────────────────────────
//  Watering session record
// ─────────────────────────────────────────────────────────────
struct WateringSession {
    uint32_t startMs;
    float    moistureBefore;
    float    moistureAfter;
    uint8_t  pulseCount;
    float    totalWaterMl;
    bool     targetReached;
    bool     aborted;
    char     abortReason[32];
};

// ─────────────────────────────────────────────────────────────
class IrrigationController {
public:
    IrrigationController();

    // Connect all dependencies (call before begin())
    void setSoilSensor   (SoilMoisture* s)   { _soil  = s; }
    void setWaterLevel   (WaterLevel*   s)   { _level = s; }
    void setFlowSensor   (FlowSensor*   s)   { _flow  = s; }
    void setDryingModel  (DryingModel*  m)   { _model = m; }
    void setDataLogger   (DataLogger*   l)   { _log   = l; }

    // Call after all setSensor*() calls
    void begin(const PlantProfile* profile);

    // ── Main loop tick — non-blocking ─────────────────────────
    void tick(uint32_t nowMs);

    // ── Public controls ───────────────────────────────────────
    void startManualWatering();          // User-initiated — respects all safety limits
    void emergencyStop(const char* reason = "user");
    bool setProfile(const PlantProfile* profile);

    void enableAutoWatering(bool enable)  { _autoWateringEnabled = enable; }
    void enableAdaptive(bool enable)      { _adaptiveEnabled     = enable; }

    // ── Calibration mode ──────────────────────────────────────
    void enterCalibrationMode();
    void exitCalibrationMode();

    // ── State queries ─────────────────────────────────────────
    IrrigationState  getState()        const { return _state; }
    PlantStatus      getPlantStatus()  const { return _plantStatus; }
    const char*      getStateStr()     const;
    const char*      getPlantStatusStr() const;
    const char*      getStatusEmoji()  const;

    bool             isPumpOn()        const { return _pumpOn; }
    bool             isAutoEnabled()   const { return _autoWateringEnabled; }
    bool             isAdaptiveEnabled()const{ return _adaptiveEnabled; }

    // Accumulated daily water (mL) — estimated from pulse duration and profile
    float            getDailyWaterMl() const { return _dailyWaterMl; }

    // Last completed watering session
    const WateringSession& getLastSession() const { return _lastSession; }

    // Last error message (for dashboard)
    const char*      getLastError()    const { return _lastError; }

    // Estimated minutes until watering needed (NAN if unknown)
    float            getMinutesUntilWater() const;

    // How many pulses remain in current session
    uint8_t          getCurrentPulse() const { return _currentPulse; }

    // Timestamp of last watering (millis)
    uint32_t         getLastWateringMs() const { return _lastWateringMs; }

    // Daily water reset (call at midnight)
    void             resetDailyWater();

private:
    // ── Dependencies ──────────────────────────────────────────
    SoilMoisture*  _soil;
    WaterLevel*    _level;
    FlowSensor*    _flow;
    DryingModel*   _model;
    DataLogger*    _log;

    const PlantProfile* _profile;

    // ── State machine ─────────────────────────────────────────
    IrrigationState _state;
    PlantStatus     _plantStatus;
    IrrigationState _nextState;   // Deferred transition

    // ── Pump control ──────────────────────────────────────────
    bool     _pumpOn;
    uint32_t _pumpStartMs;        // When current pulse began
    uint32_t _sessionPumpOnMs;    // Total pump-on time this session

    void _pumpON(uint32_t nowMs);
    void _pumpOFF(uint32_t nowMs);

    // ── Watering session ──────────────────────────────────────
    uint8_t  _currentPulse;
    float    _moistureBeforePulse;  // Moisture reading before last pulse
    float    _sessionStartMoisture;
    uint32_t _stabilizationStartMs;
    uint32_t _lastWateringMs;
    float    _dailyWaterMl;

    WateringSession _lastSession;
    WateringSession _currentSession;

    // ── Feature flags ─────────────────────────────────────────
    bool _autoWateringEnabled;
    bool _adaptiveEnabled;

    // ── Error tracking ────────────────────────────────────────
    char     _lastError[64];
    uint8_t  _consecutivePumpFails;
    uint8_t  _consecutiveSensorErrors;

    // ── Prediction sampling ───────────────────────────────────
    uint32_t _lastPredictionSampleMs;

    // ── State handlers ────────────────────────────────────────
    void _handleStartup      (uint32_t nowMs);
    void _handleMonitoring   (uint32_t nowMs);
    void _handleWatering     (uint32_t nowMs);
    void _handleStabilizing  (uint32_t nowMs);
    void _handleVerifying    (uint32_t nowMs);
    void _handleSensorError  (uint32_t nowMs);
    void _handlePumpError    (uint32_t nowMs);
    void _startWateringSession(uint32_t nowMs);
    void _abortWatering      (uint32_t nowMs, const char* reason);

    // ── Decision logic ────────────────────────────────────────
    PlantStatus _evaluatePlantStatus(float moisture, uint32_t nowMs);
    bool        _shouldStartWatering(float moisture, uint32_t nowMs);

    // ── Safety checks ─────────────────────────────────────────
    bool _checkSafetyInterlocks(uint32_t nowMs) const;
    bool _isDailyLimitReached()                 const;
    bool _isCooldownActive(uint32_t nowMs)      const;

    // ── Helpers ───────────────────────────────────────────────
    void _transitionTo(IrrigationState newState, uint32_t nowMs);
    void _setError(const char* msg);
    float _estimateWaterMl(uint32_t pulseDurationMs) const;

    // Approximate mL per millisecond of pump runtime (calibration)
    // Based on typical small pumps: ~100–200 mL/min → ~1.5–3.3 mL/s
    static constexpr float ML_PER_MS = 0.002f;  // ~2 mL/s = 120 mL/min
};
