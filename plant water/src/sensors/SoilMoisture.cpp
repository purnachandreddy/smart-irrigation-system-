// =============================================================
//  SoilMoisture.cpp  —  Capacitive soil moisture sensor module
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#include "SoilMoisture.h"
#include <Arduino.h>
#include <cmath>

// ─── Valid ADC range sanity bounds (hardware: 12-bit = 0–4095) ───────────────
// A capacitive sensor never reads exactly 0 or 4095 in normal use.
// Values near the rails indicate disconnection.
static constexpr int ADC_MIN_VALID = 100;
static constexpr int ADC_MAX_VALID = 4000;

// ─────────────────────────────────────────────────────────────
SoilMoisture::SoilMoisture()
    : _pin(0), _dryValue(DEFAULT_SOIL_DRY_VALUE), _wetValue(DEFAULT_SOIL_WET_VALUE),
      _rawADC(0), _unfilteredPct(0.0f), _filteredPct(50.0f),
      _emaAlpha(SOIL_EMA_ALPHA),
      _lastSampleMs(0), _invalidCount(0),
      _state(SoilSensorState::OK)
{}

// ─────────────────────────────────────────────────────────────
void SoilMoisture::begin(uint8_t pin, int dryValue, int wetValue) {
    _pin      = pin;
    _dryValue = dryValue;
    _wetValue = wetValue;

    // ESP32 ADC — configure 12-bit resolution & attenuation (0–3.3 V range)
    analogReadResolution(12);
#if defined(ADC_ATTEN_DB_11)
    analogSetPinAttenuation(_pin, ADC_ATTEN_DB_11);
#else
    analogSetPinAttenuation(_pin, ADC_11db);
#endif

    // Warm up: take an initial reading to seed the EMA
    int raw = _multiSampleADC(16);
    _rawADC = raw;
    if (_isRawValid(raw)) {
        _unfilteredPct = _adcToPercent(raw);
        _filteredPct   = _unfilteredPct;  // seed EMA with first good reading
        _state         = SoilSensorState::OK;
    } else {
        _state = SoilSensorState::DISCONNECTED;
        LOG("SoilMoisture: initial raw=%d is invalid (disconnected?)", raw);
    }
    LOG("SoilMoisture init — pin:%d dry:%d wet:%d firstPct:%.1f%%",
        pin, dryValue, wetValue, _filteredPct);
}

// ─────────────────────────────────────────────────────────────
void SoilMoisture::tick(uint32_t nowMs) {
    // Only sample at defined intervals — non-blocking
    if ((nowMs - _lastSampleMs) < SENSOR_SAMPLE_INTERVAL_MS) return;
    _lastSampleMs = nowMs;

    int raw = _multiSampleADC(8);
    _rawADC = raw;

    if (!_isRawValid(raw)) {
        _invalidCount++;
        LOG("SoilMoisture: invalid reading raw=%d (%d/%d)",
            raw, _invalidCount, SOIL_INVALID_COUNT_MAX);
        if (_invalidCount >= SOIL_INVALID_COUNT_MAX) {
            _state = SoilSensorState::ERROR;
        } else if (_state == SoilSensorState::OK) {
            _state = SoilSensorState::DISCONNECTED;
        }
        return;
    }

    // Valid reading — reset error counter
    _invalidCount = 0;
    float pct = _adcToPercent(raw);
    _unfilteredPct = pct;

    // Sanity: reject sudden jumps > 30% in one tick (sensor glitch)
    float delta = fabsf(pct - _filteredPct);
    if (delta > 30.0f && _state == SoilSensorState::OK) {
        LOG("SoilMoisture: spike detected delta=%.1f%% — ignoring", delta);
        _state = SoilSensorState::NOISY;
        return;
    }

    _updateEMA(pct);

    // Recover from NOISY/DISCONNECTED if we see several good readings
    if (_state != SoilSensorState::OK && delta <= 30.0f) {
        _state = SoilSensorState::OK;
    }

    LOG("SoilMoisture: raw=%d  unfiltered=%.1f%%  filtered=%.1f%%",
        raw, _unfilteredPct, _filteredPct);
}

// ─────────────────────────────────────────────────────────────
float SoilMoisture::getPercentage() const { return _filteredPct; }
int   SoilMoisture::getRawADC()     const { return _rawADC; }
float SoilMoisture::getUnfiltered() const { return _unfilteredPct; }

// ─────────────────────────────────────────────────────────────
SoilSensorState SoilMoisture::getState() const { return _state; }

bool SoilMoisture::isValid() const {
    return (_state == SoilSensorState::OK || _state == SoilSensorState::NOISY);
}

const char* SoilMoisture::getStateStr() const {
    switch (_state) {
        case SoilSensorState::OK:          return "OK";
        case SoilSensorState::CALIBRATING: return "CALIBRATING";
        case SoilSensorState::NOISY:       return "NOISY";
        case SoilSensorState::DISCONNECTED:return "DISCONNECTED";
        case SoilSensorState::ERROR:       return "ERROR";
        default:                           return "UNKNOWN";
    }
}

// ─────────────────────────────────────────────────────────────
void SoilMoisture::setCalibration(int dryValue, int wetValue) {
    _dryValue = dryValue;
    _wetValue = wetValue;
    LOG("SoilMoisture: calibration updated dry=%d wet=%d", dryValue, wetValue);
}

int SoilMoisture::sampleForCalibration() {
    _state = SoilSensorState::CALIBRATING;
    int raw = _multiSampleADC(32);  // Average more samples for precision
    LOG("SoilMoisture: calibration sample raw=%d", raw);
    _state = SoilSensorState::OK;
    return raw;
}

// ─────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────

// Average multiple ADC readings to reduce noise
int SoilMoisture::_multiSampleADC(uint8_t samples) {
    long sum = 0;
    for (uint8_t i = 0; i < samples; i++) {
        sum += analogRead(_pin);
        delayMicroseconds(200);  // brief pause between samples
    }
    return (int)(sum / samples);
}

// Convert raw ADC to moisture percentage
// Capacitive sensors: higher ADC = drier  (inverted)
float SoilMoisture::_adcToPercent(int raw) const {
    // Clamp to calibrated range
    raw = constrain(raw, _wetValue, _dryValue);
    // Map: dryValue → 0%, wetValue → 100%
    float pct = (float)(_dryValue - raw) / (float)(_dryValue - _wetValue) * 100.0f;
    return constrain(pct, 0.0f, 100.0f);
}

bool SoilMoisture::_isRawValid(int raw) const {
    return (raw >= ADC_MIN_VALID && raw <= ADC_MAX_VALID);
}

// Exponential Moving Average update
void SoilMoisture::_updateEMA(float newPct) {
    _filteredPct = (_emaAlpha * newPct) + ((1.0f - _emaAlpha) * _filteredPct);
}
