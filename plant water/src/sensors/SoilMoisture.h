// =============================================================
//  SoilMoisture.h  —  Capacitive soil moisture sensor module
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#pragma once
#include <Arduino.h>
#include "../config.h"

// ─────────────────────────────────────────────────────────────
//  Sensor validity states
// ─────────────────────────────────────────────────────────────
enum class SoilSensorState : uint8_t {
    OK,              // Normal operation
    CALIBRATING,     // User is in calibration mode
    NOISY,           // Readings pass but show unusual variance
    DISCONNECTED,    // ADC stuck at rail or impossible value
    ERROR            // Consecutive invalid readings exceeded threshold
};

// ─────────────────────────────────────────────────────────────
//  SoilMoisture
// ─────────────────────────────────────────────────────────────
class SoilMoisture {
public:
    SoilMoisture();

    // Initialise — pass pin, stored dry/wet calibration values
    void begin(uint8_t pin, int dryValue, int wetValue);

    // Non-blocking tick — call every loop iteration
    // Takes a reading if sampleIntervalMs has elapsed
    void tick(uint32_t nowMs);

    // ── Readings ──────────────────────────────────────────────
    float  getPercentage()   const;  // 0.0–100.0 % (EMA filtered)
    int    getRawADC()       const;  // Latest raw ADC value
    float  getUnfiltered()   const;  // Latest percentage before EMA

    // ── Sensor health ─────────────────────────────────────────
    SoilSensorState getState()    const;
    bool            isValid()     const;  // true when state == OK or NOISY
    const char*     getStateStr() const;

    // ── Calibration ───────────────────────────────────────────
    void  setCalibration(int dryValue, int wetValue);
    int   getDryValue()  const { return _dryValue; }
    int   getWetValue()  const { return _wetValue; }

    // Call to capture a live calibration sample
    int   sampleForCalibration();  // returns raw ADC

private:
    uint8_t _pin;
    int     _dryValue;
    int     _wetValue;

    int     _rawADC;
    float   _unfilteredPct;
    float   _filteredPct;       // EMA output
    float   _emaAlpha;

    uint32_t _lastSampleMs;
    uint8_t  _invalidCount;

    SoilSensorState _state;

    // Internal helpers
    int   _multiSampleADC(uint8_t samples = 8);
    float _adcToPercent(int raw) const;
    bool  _isRawValid(int raw)   const;
    void  _updateEMA(float newPct);
};
