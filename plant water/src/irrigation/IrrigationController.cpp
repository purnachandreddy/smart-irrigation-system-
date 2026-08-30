// =============================================================
//  IrrigationController.cpp  —  Core state machine & pump control
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#include "IrrigationController.h"
#include <cstring>
#include <cmath>

// ─────────────────────────────────────────────────────────────
//  Constructor
// ─────────────────────────────────────────────────────────────
IrrigationController::IrrigationController()
    : _soil(nullptr), _level(nullptr), _flow(nullptr),
      _model(nullptr), _log(nullptr), _profile(nullptr),
      _state(IrrigationState::STARTUP), _plantStatus(PlantStatus::HEALTHY),
      _nextState(IrrigationState::STARTUP),
      _pumpOn(false), _pumpStartMs(0), _sessionPumpOnMs(0),
      _currentPulse(0), _moistureBeforePulse(0.0f),
      _sessionStartMoisture(0.0f), _stabilizationStartMs(0),
      _lastWateringMs(0), _dailyWaterMl(0.0f),
      _autoWateringEnabled(true), _adaptiveEnabled(true),
      _consecutivePumpFails(0), _consecutiveSensorErrors(0),
      _lastPredictionSampleMs(0)
{
    memset(&_lastSession,    0, sizeof(_lastSession));
    memset(&_currentSession, 0, sizeof(_currentSession));
    memset(_lastError, 0, sizeof(_lastError));
}

// ─────────────────────────────────────────────────────────────
void IrrigationController::begin(const PlantProfile* profile) {
    _profile = profile ? profile : &PROFILE_NORMAL;

    // CRITICAL: Pump must be OFF at startup, unconditionally
    pinMode(PUMP_PIN, OUTPUT);
    digitalWrite(PUMP_PIN, LOW);
    _pumpOn = false;

    // Status LED
    pinMode(STATUS_LED_PIN, OUTPUT);
    digitalWrite(STATUS_LED_PIN, LOW);

    _state = IrrigationState::STARTUP;
    LOG("IrrigationController: started — profile=%s", _profile->name);
}

// ─────────────────────────────────────────────────────────────
//  Main loop tick — must be called every loop() iteration
// ─────────────────────────────────────────────────────────────
void IrrigationController::tick(uint32_t nowMs) {
    // ── Safety: absolute pump runtime guard ───────────────────
    if (_pumpOn) {
        uint32_t onTime = nowMs - _pumpStartMs;
        if (onTime > ABSOLUTE_MAX_PUMP_RUNTIME_MS) {
            _pumpOFF(nowMs);
            _setError("Safety: max pump runtime exceeded");
            _transitionTo(IrrigationState::PUMP_ERROR, nowMs);
            return;
        }
        // Flow fault check (if sensor available)
        if (_flow && _flow->isNoFlowFault()) {
            _pumpOFF(nowMs);
            _setError("No water flow detected with pump ON");
            _transitionTo(IrrigationState::PUMP_ERROR, nowMs);
            return;
        }
    }

    // ── Periodic prediction model sampling ────────────────────
    if (_state == IrrigationState::MONITORING ||
        _state == IrrigationState::DRYING     ||
        _state == IrrigationState::WATER_SOON) {
        if ((nowMs - _lastPredictionSampleMs) >= PREDICTION_SAMPLE_INTERVAL_MS) {
            _lastPredictionSampleMs = nowMs;
            if (_soil && _soil->isValid()) {
                _model->addReading(nowMs, _soil->getPercentage());
            }
        }
    }

    // ── State dispatch ────────────────────────────────────────
    switch (_state) {
        case IrrigationState::STARTUP:
            _handleStartup(nowMs);
            break;

        case IrrigationState::CALIBRATING:
            // Stays here until exitCalibrationMode() is called
            break;

        case IrrigationState::MONITORING:
        case IrrigationState::DRYING:
        case IrrigationState::WATER_SOON:
        case IrrigationState::WATERING_COMPLETE:
            _handleMonitoring(nowMs);
            break;

        case IrrigationState::WATERING:
            _handleWatering(nowMs);
            break;

        case IrrigationState::STABILIZING:
            _handleStabilizing(nowMs);
            break;

        case IrrigationState::VERIFYING:
            _handleVerifying(nowMs);
            break;

        case IrrigationState::SENSOR_ERROR:
            _handleSensorError(nowMs);
            break;

        case IrrigationState::PUMP_ERROR:
            _handlePumpError(nowMs);
            break;

        case IrrigationState::LOW_WATER:
            // Stay here, still evaluate plant status but don't water
            if (_level && _level->isSafeToWater()) {
                _transitionTo(IrrigationState::MONITORING, nowMs);
            }
            break;

        case IrrigationState::CRITICAL_ERROR:
            // Everything off — stay here until user resets via dashboard
            if (_pumpOn) _pumpOFF(nowMs);
            break;
    }
}

// ─────────────────────────────────────────────────────────────
//  STARTUP handler
//  Wait for sensors to settle, validate, then enter MONITORING
// ─────────────────────────────────────────────────────────────
void IrrigationController::_handleStartup(uint32_t nowMs) {
    // Need at least 3 sensor ticks to settle EMA
    static uint8_t startupTicks = 0;
    startupTicks++;

    if (startupTicks < 3) {
        Serial.printf("[%lu] STARTUP: warming up sensors (%d/3)\n", nowMs, startupTicks);
        return;
    }

    if (!_soil) {
        _setError("Soil sensor not attached");
        _transitionTo(IrrigationState::CRITICAL_ERROR, nowMs);
        return;
    }

    if (!_soil->isValid()) {
        _consecutiveSensorErrors++;
        if (_consecutiveSensorErrors >= 3) {
            _setError("Soil sensor invalid on startup");
            _transitionTo(IrrigationState::SENSOR_ERROR, nowMs);
            return;
        }
        return;
    }

    _consecutiveSensorErrors = 0;

    // Check water level
    if (_level && !_level->isSafeToWater()) {
        Serial.printf("[%lu] STARTUP: tank empty — entering LOW_WATER\n", nowMs);
        _transitionTo(IrrigationState::LOW_WATER, nowMs);
        return;
    }

    // All good — enter monitoring
    if (_log) {
        _log->logEvent(LogEventType::SYSTEM_BOOT,
                       _soil->getPercentage(),
                       _level ? _level->getPercentage() : 100.0f,
                       false, 0.0f, "Boot OK");
    }
    _transitionTo(IrrigationState::MONITORING, nowMs);
}

// ─────────────────────────────────────────────────────────────
//  MONITORING handler
//  Continuously evaluates plant status and decides whether to water.
// ─────────────────────────────────────────────────────────────
void IrrigationController::_handleMonitoring(uint32_t nowMs) {
    // ── Sensor health check ───────────────────────────────────
    if (_soil && !_soil->isValid()) {
        _consecutiveSensorErrors++;
        if (_consecutiveSensorErrors >= SOIL_INVALID_COUNT_MAX) {
            _setError("Soil sensor disconnected or failed");
            _transitionTo(IrrigationState::SENSOR_ERROR, nowMs);
            return;
        }
    } else {
        _consecutiveSensorErrors = 0;
    }

    // ── Tank level check ──────────────────────────────────────
    if (_level) {
        WaterLevelState lvl = _level->getState();
        if (lvl == WaterLevelState::LEVEL_EMPTY) {
            _setError("Water tank empty");
            _transitionTo(IrrigationState::LOW_WATER, nowMs);
            return;
        }
    }

    if (!_soil) return;

    float moisture = _soil->getPercentage();
    _plantStatus   = _evaluatePlantStatus(moisture, nowMs);

    // ── State assignment based on status ─────────────────────
    IrrigationState targetState = IrrigationState::MONITORING;
    switch (_plantStatus) {
        case PlantStatus::HEALTHY:   targetState = IrrigationState::MONITORING;  break;
        case PlantStatus::DRYING:    targetState = IrrigationState::DRYING;      break;
        case PlantStatus::WATER_SOON:targetState = IrrigationState::WATER_SOON;  break;
        default:                     targetState = IrrigationState::MONITORING;  break;
    }
    _state = targetState;  // lightweight update, no full transition

    // ── Decide whether to start watering ─────────────────────
    if (_shouldStartWatering(moisture, nowMs)) {
        _startWateringSession(nowMs);
    }
}

// ─────────────────────────────────────────────────────────────
//  Decision: evaluate plant health status
// ─────────────────────────────────────────────────────────────
PlantStatus IrrigationController::_evaluatePlantStatus(float moisture, uint32_t nowMs) {
    if (!_profile) return PlantStatus::ERROR;

    // Critical dryness — immediate action needed
    if (moisture <= _profile->criticalDryness) return PlantStatus::CRITICAL;

    // Below minimum acceptable moisture
    if (moisture <= _profile->minMoisture)     return PlantStatus::NEEDS_WATER;

    // Within healthy range — check trend
    if (moisture <= _profile->targetMoisture) {
        // Below target but above minimum — check if drying fast
        if (_adaptiveEnabled && _model && _model->isDataSufficient()) {
            float rate = _model->getDryingRatePctPerHour();
            if (!isnan(rate) && rate > _profile->dryingRateWarnThreshold) {
                float minsLeft = _model->getMinutesUntilCritical(moisture, _profile->minMoisture);
                if (!isnan(minsLeft) && minsLeft < 120.0f) {
                    return PlantStatus::WATER_SOON;  // <2 hrs predicted
                }
                return PlantStatus::DRYING;
            }
        }
        return PlantStatus::DRYING;  // below target, not urgent yet
    }

    // Above target — healthy
    return PlantStatus::HEALTHY;
}

// ─────────────────────────────────────────────────────────────
//  Decision: should we start a watering session now?
// ─────────────────────────────────────────────────────────────
bool IrrigationController::_shouldStartWatering(float moisture, uint32_t nowMs) {
    if (!_autoWateringEnabled)          return false;
    if (!_checkSafetyInterlocks(nowMs)) return false;

    // Must be in a state where auto-watering makes sense
    if (_state != IrrigationState::MONITORING &&
        _state != IrrigationState::DRYING     &&
        _state != IrrigationState::WATER_SOON) return false;

    // Immediate water needed
    if (_plantStatus == PlantStatus::NEEDS_WATER ||
        _plantStatus == PlantStatus::CRITICAL) {
        LOG("Decision: WATER NOW — moisture=%.1f%% status=%s", moisture, getPlantStatusStr());
        return true;
    }

    // Adaptive prediction: if <30 min until critical, water proactively
    if (_adaptiveEnabled && _plantStatus == PlantStatus::WATER_SOON && _model) {
        float minsLeft = _model->getMinutesUntilCritical(moisture, _profile->minMoisture);
        if (!isnan(minsLeft) && minsLeft < 30.0f) {
            LOG("Decision: PROACTIVE WATER — %.0f min until critical", minsLeft);
            return true;
        }
    }

    return false;
}

// ─────────────────────────────────────────────────────────────
//  Safety interlock checks
// ─────────────────────────────────────────────────────────────
bool IrrigationController::_checkSafetyInterlocks(uint32_t nowMs) const {
    if (!_profile)  return false;

    // Soil sensor must be valid
    if (_soil && !_soil->isValid()) return false;

    // Tank must have water
    if (_level && !_level->isSafeToWater()) return false;

    // Daily water limit
    if (_isDailyLimitReached()) return false;

    // Cooldown since last watering
    if (_isCooldownActive(nowMs)) return false;

    return true;
}

bool IrrigationController::_isDailyLimitReached() const {
    if (!_profile) return false;
    uint16_t limit = min((uint16_t)ABSOLUTE_MAX_DAILY_WATER_ML, _profile->maxDailyWaterMl);
    return (_dailyWaterMl >= (float)limit);
}

bool IrrigationController::_isCooldownActive(uint32_t nowMs) const {
    if (!_profile) return false;
    if (_lastWateringMs == 0) return false;
    return ((nowMs - _lastWateringMs) < _profile->wateringCooldownMs);
}

// ─────────────────────────────────────────────────────────────
//  Start a new watering session
// ─────────────────────────────────────────────────────────────
void IrrigationController::_startWateringSession(uint32_t nowMs) {
    _currentSession             = {};
    _currentSession.startMs     = nowMs;
    _currentSession.moistureBefore = _soil ? _soil->getPercentage() : 0.0f;
    _sessionStartMoisture       = _currentSession.moistureBefore;
    _currentPulse               = 0;
    _sessionPumpOnMs            = 0;

    if (_flow) _flow->resetSession();

    Serial.printf("[%lu] === WATERING SESSION START — moisture=%.1f%% target=%.1f%% ===\n",
        nowMs, _sessionStartMoisture, _profile->targetMoisture);

    if (_log) {
        _log->logEvent(LogEventType::WATERING_START,
                       _sessionStartMoisture,
                       _level ? _level->getPercentage() : 100.0f,
                       false, 0.0f, "Session start");
    }

    _transitionTo(IrrigationState::WATERING, nowMs);
}

// ─────────────────────────────────────────────────────────────
//  WATERING state — fire a pump pulse
// ─────────────────────────────────────────────────────────────
void IrrigationController::_handleWatering(uint32_t nowMs) {
    if (_pumpOn) {
        // Pump is currently ON — check if pulse duration has elapsed
        uint32_t onTime = nowMs - _pumpStartMs;
        if (onTime >= _profile->pulseDurationMs) {
            _pumpOFF(nowMs);
            _sessionPumpOnMs += onTime;

            float waterMl = _estimateWaterMl(onTime);
            _currentSession.totalWaterMl += waterMl;
            _dailyWaterMl                += waterMl;
            _currentSession.pulseCount++;

            Serial.printf("[%lu] Pump OFF — pulse #%d (%lu ms) +%.1f mL daily=%.0f mL\n",
                nowMs, _currentPulse, onTime, waterMl, _dailyWaterMl);

            if (_log) {
                char note[48];
                snprintf(note, sizeof(note), "Pulse #%d  +%.1fmL", _currentPulse, waterMl);
                _log->logEvent(LogEventType::WATERING_PULSE,
                               _soil ? _soil->getPercentage() : 0.0f,
                               _level ? _level->getPercentage() : 100.0f,
                               false, waterMl, note);
            }

            // Start stabilization delay
            _moistureBeforePulse     = _soil ? _soil->getPercentage() : 0.0f;
            _stabilizationStartMs    = nowMs;
            _transitionTo(IrrigationState::STABILIZING, nowMs);
        }
        // else: still counting pulse duration — do nothing
        return;
    }

    // Pump is OFF — check if we should fire another pulse
    // Safety checks before each pulse
    if (!_checkSafetyInterlocks(nowMs) || _isDailyLimitReached()) {
        _abortWatering(nowMs, "Safety interlock during session");
        return;
    }
    if (_currentPulse >= min((uint8_t)ABSOLUTE_MAX_WATERING_CYCLES, _profile->maxPulsesPerSession)) {
        _abortWatering(nowMs, "Max pulse count reached");
        return;
    }
    if (_sessionPumpOnMs >= min((uint32_t)ABSOLUTE_MAX_PUMP_RUNTIME_MS, _profile->maxPumpRuntimeMs)) {
        _abortWatering(nowMs, "Session pump runtime limit reached");
        return;
    }

    // Fire the pulse
    _currentPulse++;
    Serial.printf("[%lu] Pump pulse #%d starting (%lu ms)\n",
        nowMs, _currentPulse, _profile->pulseDurationMs);
    _pumpON(nowMs);
}

// ─────────────────────────────────────────────────────────────
//  STABILIZING state — wait for water to distribute
// ─────────────────────────────────────────────────────────────
void IrrigationController::_handleStabilizing(uint32_t nowMs) {
    uint32_t elapsed = nowMs - _stabilizationStartMs;

    if (elapsed < _profile->stabilizationDelayMs) return;  // still waiting

    Serial.printf("[%lu] Stabilization complete — reading moisture\n", nowMs);
    _transitionTo(IrrigationState::VERIFYING, nowMs);
}

// ─────────────────────────────────────────────────────────────
//  VERIFYING state — check if moisture rose, decide next step
// ─────────────────────────────────────────────────────────────
void IrrigationController::_handleVerifying(uint32_t nowMs) {
    if (!_soil) {
        _abortWatering(nowMs, "Sensor unavailable for verification");
        return;
    }

    float moistureNow = _soil->getPercentage();
    float delta       = moistureNow - _moistureBeforePulse;

    Serial.printf("[%lu] VERIFY — before=%.1f%%  now=%.1f%%  delta=+%.1f%%\n",
        nowMs, _moistureBeforePulse, moistureNow, delta);

    // ── No rise detected ─────────────────────────────────────
    if (delta < 1.0f) {
        _consecutivePumpFails++;
        Serial.printf("[%lu] No moisture increase! (%d consecutive)\n",
            nowMs, _consecutivePumpFails);
        if (_consecutivePumpFails >= 2) {
            _setError("Pump not raising moisture — tank empty/blocked/failed?");
            _currentSession.aborted = true;
            strncpy(_currentSession.abortReason, "No moisture rise", sizeof(_currentSession.abortReason) - 1);
            _lastSession = _currentSession;
            _transitionTo(IrrigationState::PUMP_ERROR, nowMs);
            if (_log) {
                _log->logEvent(LogEventType::WATERING_FAILED,
                               moistureNow,
                               _level ? _level->getPercentage() : 100.0f,
                               false, _currentSession.totalWaterMl, _lastError);
            }
            return;
        }
    } else {
        _consecutivePumpFails = 0;  // moisture rose — reset fault counter
    }

    // ── Target reached — stop watering ───────────────────────
    if (moistureNow >= _profile->targetMoisture) {
        _currentSession.moistureAfter = moistureNow;
        _currentSession.targetReached = true;
        _lastSession                  = _currentSession;
        _lastWateringMs               = nowMs;

        Serial.printf("[%lu] TARGET REACHED — moisture=%.1f%%  total=%.1f mL\n",
            nowMs, moistureNow, _currentSession.totalWaterMl);

        if (_model) _model->notifyWatering(nowMs);

        if (_log) {
            char note[48];
            snprintf(note, sizeof(note), "%.1f->%.1f%% +%.0fmL",
                _sessionStartMoisture, moistureNow, _currentSession.totalWaterMl);
            _log->logEvent(LogEventType::WATERING_DONE,
                           moistureNow,
                           _level ? _level->getPercentage() : 100.0f,
                           false, _currentSession.totalWaterMl, note);
        }

        _transitionTo(IrrigationState::WATERING_COMPLETE, nowMs);
        return;
    }

    // ── Not at target yet — check if we can pulse again ──────
    if (_currentPulse >= min((uint8_t)ABSOLUTE_MAX_WATERING_CYCLES, _profile->maxPulsesPerSession)) {
        _currentSession.moistureAfter = moistureNow;
        _lastSession = _currentSession;
        _lastWateringMs = nowMs;
        Serial.printf("[%lu] Max pulses reached — stopping at %.1f%%\n", nowMs, moistureNow);
        _transitionTo(IrrigationState::WATERING_COMPLETE, nowMs);
        return;
    }

    // ── Fire another pulse ────────────────────────────────────
    Serial.printf("[%lu] Below target (%.1f%% < %.1f%%) — another pulse\n",
        nowMs, moistureNow, _profile->targetMoisture);
    _transitionTo(IrrigationState::WATERING, nowMs);
}

// ─────────────────────────────────────────────────────────────
//  SENSOR_ERROR handler — periodically attempt recovery
// ─────────────────────────────────────────────────────────────
void IrrigationController::_handleSensorError(uint32_t nowMs) {
    if (_pumpOn) _pumpOFF(nowMs);  // always keep pump off in error state

    // Check if sensor has recovered
    if (_soil && _soil->isValid()) {
        _consecutiveSensorErrors = 0;
        Serial.printf("[%lu] Sensor recovered — returning to MONITORING\n", nowMs);
        _transitionTo(IrrigationState::MONITORING, nowMs);
    }
}

// ─────────────────────────────────────────────────────────────
//  PUMP_ERROR handler
// ─────────────────────────────────────────────────────────────
void IrrigationController::_handlePumpError(uint32_t nowMs) {
    if (_pumpOn) _pumpOFF(nowMs);

    _consecutivePumpFails++;
    if (_consecutivePumpFails >= 3) {
        _setError("Repeated pump failures — entering CRITICAL_ERROR");
        _transitionTo(IrrigationState::CRITICAL_ERROR, nowMs);
    }
    // Otherwise stay in PUMP_ERROR — will attempt recovery on next watering need
}

// ─────────────────────────────────────────────────────────────
//  Abort current watering session
// ─────────────────────────────────────────────────────────────
void IrrigationController::_abortWatering(uint32_t nowMs, const char* reason) {
    if (_pumpOn) _pumpOFF(nowMs);
    _setError(reason);
    _currentSession.aborted = true;
    strncpy(_currentSession.abortReason, reason, sizeof(_currentSession.abortReason) - 1);
    _lastSession = _currentSession;
    _lastWateringMs = nowMs;

    Serial.printf("[%lu] WATERING ABORTED: %s\n", nowMs, reason);

    if (_log) {
        _log->logEvent(LogEventType::WATERING_FAILED,
                       _soil ? _soil->getPercentage() : 0.0f,
                       _level ? _level->getPercentage() : 100.0f,
                       false, _currentSession.totalWaterMl, reason);
    }

    _transitionTo(IrrigationState::MONITORING, nowMs);
}

// ─────────────────────────────────────────────────────────────
//  Public controls
// ─────────────────────────────────────────────────────────────
void IrrigationController::startManualWatering() {
    uint32_t nowMs = millis();

    if (!_checkSafetyInterlocks(nowMs)) {
        _setError("Manual water blocked by safety interlock");
        Serial.printf("[%lu] Manual water BLOCKED: %s\n", nowMs, _lastError);
        return;
    }

    if (_state == IrrigationState::WATERING ||
        _state == IrrigationState::STABILIZING ||
        _state == IrrigationState::VERIFYING) {
        Serial.printf("[%lu] Manual water: already watering\n", nowMs);
        return;
    }

    Serial.printf("[%lu] === MANUAL WATERING START ===\n", nowMs);
    if (_log) {
        _log->logEvent(LogEventType::MANUAL_WATER,
                       _soil ? _soil->getPercentage() : 0.0f,
                       _level ? _level->getPercentage() : 100.0f,
                       false, 0.0f, "User initiated");
    }

    _startWateringSession(nowMs);
}

void IrrigationController::emergencyStop(const char* reason) {
    uint32_t nowMs = millis();
    _pumpOFF(nowMs);
    _setError(reason ? reason : "Emergency stop");
    _transitionTo(IrrigationState::MONITORING, nowMs);
    Serial.printf("[%lu] EMERGENCY STOP: %s\n", nowMs, _lastError);
}

bool IrrigationController::setProfile(const PlantProfile* profile) {
    if (!profile) return false;
    _profile = profile;
    LOG("IrrigationController: profile changed to %s", _profile->name);
    return true;
}

void IrrigationController::enterCalibrationMode() {
    if (_pumpOn) emergencyStop("entering calibration");
    _transitionTo(IrrigationState::CALIBRATING, millis());
}

void IrrigationController::exitCalibrationMode() {
    _transitionTo(IrrigationState::STARTUP, millis());
}

void IrrigationController::resetDailyWater() {
    _dailyWaterMl = 0.0f;
    if (_flow) _flow->resetDaily();
    LOG("IrrigationController: daily water reset");
}

// ─────────────────────────────────────────────────────────────
//  Pump control helpers
// ─────────────────────────────────────────────────────────────
void IrrigationController::_pumpON(uint32_t nowMs) {
    if (_pumpOn) return;
    digitalWrite(PUMP_PIN, HIGH);
    digitalWrite(STATUS_LED_PIN, HIGH);
    _pumpOn      = true;
    _pumpStartMs = nowMs;
    if (_flow) _flow->notifyPumpOn(nowMs);
    LOG("PUMP ON");
}

void IrrigationController::_pumpOFF(uint32_t nowMs) {
    if (!_pumpOn) return;
    digitalWrite(PUMP_PIN, LOW);
    digitalWrite(STATUS_LED_PIN, LOW);
    _pumpOn = false;
    if (_flow) _flow->notifyPumpOff();
    LOG("PUMP OFF");
}

// ─────────────────────────────────────────────────────────────
//  Static state name helper (lambdas cannot be passed as printf varargs)
// ─────────────────────────────────────────────────────────────
static const char* stateToStr(IrrigationState s) {
    switch(s) {
        case IrrigationState::STARTUP:           return "STARTUP";
        case IrrigationState::CALIBRATING:       return "CALIBRATING";
        case IrrigationState::MONITORING:        return "MONITORING";
        case IrrigationState::DRYING:            return "DRYING";
        case IrrigationState::WATER_SOON:        return "WATER_SOON";
        case IrrigationState::WATERING:          return "WATERING";
        case IrrigationState::STABILIZING:       return "STABILIZING";
        case IrrigationState::VERIFYING:         return "VERIFYING";
        case IrrigationState::WATERING_COMPLETE: return "WATERING_COMPLETE";
        case IrrigationState::LOW_WATER:         return "LOW_WATER";
        case IrrigationState::SENSOR_ERROR:      return "SENSOR_ERROR";
        case IrrigationState::PUMP_ERROR:        return "PUMP_ERROR";
        case IrrigationState::CRITICAL_ERROR:    return "CRITICAL_ERROR";
        default:                                 return "?";
    }
}

void IrrigationController::_transitionTo(IrrigationState newState, uint32_t nowMs) {
    if (_state == newState) return;
    Serial.printf("[%lu] State: %s -> %s\n", nowMs, getStateStr(), stateToStr(newState));
    _state = newState;
}

// ─────────────────────────────────────────────────────────────
//  String representations
// ─────────────────────────────────────────────────────────────
const char* IrrigationController::getStateStr() const {
    switch (_state) {
        case IrrigationState::STARTUP:           return "STARTUP";
        case IrrigationState::CALIBRATING:       return "CALIBRATING";
        case IrrigationState::MONITORING:        return "MONITORING";
        case IrrigationState::DRYING:            return "DRYING";
        case IrrigationState::WATER_SOON:        return "WATER_SOON";
        case IrrigationState::WATERING:          return "WATERING";
        case IrrigationState::STABILIZING:       return "STABILIZING";
        case IrrigationState::VERIFYING:         return "VERIFYING";
        case IrrigationState::WATERING_COMPLETE: return "WATERING_COMPLETE";
        case IrrigationState::LOW_WATER:         return "LOW_WATER";
        case IrrigationState::SENSOR_ERROR:      return "SENSOR_ERROR";
        case IrrigationState::PUMP_ERROR:        return "PUMP_ERROR";
        case IrrigationState::CRITICAL_ERROR:    return "CRITICAL_ERROR";
        default:                                 return "UNKNOWN";
    }
}

const char* IrrigationController::getPlantStatusStr() const {
    switch (_plantStatus) {
        case PlantStatus::HEALTHY:    return "Healthy";
        case PlantStatus::DRYING:     return "Drying";
        case PlantStatus::WATER_SOON: return "Water Soon";
        case PlantStatus::NEEDS_WATER:return "Needs Water";
        case PlantStatus::CRITICAL:   return "Critical";
        case PlantStatus::ERROR:      return "Error";
        default:                      return "Unknown";
    }
}

const char* IrrigationController::getStatusEmoji() const {
    switch (_plantStatus) {
        case PlantStatus::HEALTHY:    return "🟢";
        case PlantStatus::DRYING:     return "🟡";
        case PlantStatus::WATER_SOON: return "🟠";
        case PlantStatus::NEEDS_WATER:return "🔴";
        case PlantStatus::CRITICAL:   return "🆘";
        case PlantStatus::ERROR:      return "⚠️";
        default:                      return "❓";
    }
}

float IrrigationController::getMinutesUntilWater() const {
    if (!_model || !_soil) return NAN;
    if (!_adaptiveEnabled || !_model->isDataSufficient()) return NAN;
    return _model->getMinutesUntilCritical(_soil->getPercentage(), _profile->minMoisture);
}

// ─────────────────────────────────────────────────────────────
void IrrigationController::_setError(const char* msg) {
    strncpy(_lastError, msg, sizeof(_lastError) - 1);
    _lastError[sizeof(_lastError) - 1] = '\0';
    Serial.printf("[ERROR] %s\n", _lastError);
    if (_log) {
        _log->logEvent(LogEventType::FAULT_DETECTED,
                       _soil ? _soil->getPercentage() : 0.0f,
                       _level ? _level->getPercentage() : 100.0f,
                       _pumpOn, 0.0f, msg);
    }
}

float IrrigationController::_estimateWaterMl(uint32_t pulseDurationMs) const {
    return (float)pulseDurationMs * ML_PER_MS;
}
