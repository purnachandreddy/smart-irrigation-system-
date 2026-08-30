// =============================================================
//  WaterLevel.cpp  —  Water tank level sensor module
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#include "WaterLevel.h"

WaterLevel::WaterLevel()
    : _pin(0), _raw(0), _state(WaterLevelState::LEVEL_UNKNOWN), _lastSampleMs(0)
{}

void WaterLevel::begin(uint8_t pin) {
    _pin = pin;

#if WATER_LEVEL_ANALOG
    analogReadResolution(12);
#if defined(ADC_ATTEN_DB_11)
    analogSetPinAttenuation(_pin, ADC_ATTEN_DB_11);
#else
    analogSetPinAttenuation(_pin, ADC_11db);
#endif
#else
    pinMode(_pin, INPUT_PULLUP);  // float switch pulls low when water present
#endif

    // Take an initial reading
    tick(0);
    LOG("WaterLevel init — pin:%d state:%s", pin, getStateStr());
}

void WaterLevel::tick(uint32_t nowMs) {
    // Sample every 5 seconds (stagger from soil sensor)
    if (nowMs > 0 && (nowMs - _lastSampleMs) < 5000UL) return;
    _lastSampleMs = nowMs;

#if WATER_LEVEL_ANALOG
    // Average 4 samples
    long sum = 0;
    for (int i = 0; i < 4; i++) {
        sum += analogRead(_pin);
        delayMicroseconds(200);
    }
    _raw = (int)(sum / 4);
#else
    // Digital float switch: LOW = water present (pulled to ground), HIGH = empty
    _raw = digitalRead(_pin);
#endif

    _updateState();
}

WaterLevelState WaterLevel::getState()    const { return _state; }
int             WaterLevel::getRaw()      const { return _raw; }
bool            WaterLevel::isSafeToWater() const {
    return (_state != WaterLevelState::LEVEL_EMPTY && _state != WaterLevelState::LEVEL_UNKNOWN);
}

float WaterLevel::getPercentage() const {
#if WATER_LEVEL_ANALOG
    // Map ADC range to 0–100%
    // Assumes: higher ADC = more water (adjust sensor-specific)
    float pct = (float)(_raw - 0) / (float)(4095 - 0) * 100.0f;
    return constrain(pct, 0.0f, 100.0f);
#else
    // Digital: either 0% or 100%
    switch (_state) {
        case WaterLevelState::LEVEL_FULL:
        case WaterLevelState::LEVEL_NORMAL: return 100.0f;
        case WaterLevelState::LEVEL_LOW:    return 25.0f;
        case WaterLevelState::LEVEL_EMPTY:  return 0.0f;
        default:                            return 50.0f;
    }
#endif
}

const char* WaterLevel::getStateStr() const {
    switch (_state) {
        case WaterLevelState::LEVEL_FULL:    return "FULL";
        case WaterLevelState::LEVEL_NORMAL:  return "NORMAL";
        case WaterLevelState::LEVEL_LOW:     return "LOW";
        case WaterLevelState::LEVEL_EMPTY:   return "EMPTY";
        case WaterLevelState::LEVEL_UNKNOWN: return "UNKNOWN";
        default:                             return "?";
    }
}

void WaterLevel::_updateState() {
#if WATER_LEVEL_ANALOG
    if      (_raw >= WATER_LEVEL_FULL_ADC)   _state = WaterLevelState::LEVEL_FULL;
    else if (_raw >= WATER_LEVEL_NORMAL_ADC) _state = WaterLevelState::LEVEL_NORMAL;
    else if (_raw >= WATER_LEVEL_LOW_ADC)    _state = WaterLevelState::LEVEL_LOW;
    else                                     _state = WaterLevelState::LEVEL_EMPTY;
#else
    // LOW = float switch closed = water present; HIGH = open = empty
    _state = (_raw == LOW) ? WaterLevelState::LEVEL_NORMAL : WaterLevelState::LEVEL_EMPTY;
#endif
    LOG("WaterLevel: raw=%d  state=%s  pct=%.0f%%", _raw, getStateStr(), getPercentage());
}
