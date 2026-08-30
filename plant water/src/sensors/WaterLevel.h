// =============================================================
//  WaterLevel.h  —  Water tank level sensor module
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#pragma once
#include <Arduino.h>
#include "../config.h"

// ─────────────────────────────────────────────────────────────
enum class WaterLevelState : uint8_t {
    LEVEL_FULL    = 0,
    LEVEL_NORMAL  = 1,
    LEVEL_LOW     = 2,
    LEVEL_EMPTY   = 3,
    LEVEL_UNKNOWN = 4
};

// ─────────────────────────────────────────────────────────────
class WaterLevel {
public:
    WaterLevel();

    void begin(uint8_t pin);
    void tick(uint32_t nowMs);

    WaterLevelState getState()       const;
    const char*     getStateStr()    const;

    // Percentage approximation (0–100)
    float           getPercentage()  const;

    bool            isSafeToWater()  const;  // false when EMPTY

    // Raw ADC (analog mode) or raw digital (float switch mode)
    int             getRaw()         const;

private:
    uint8_t         _pin;
    int             _raw;
    WaterLevelState _state;
    uint32_t        _lastSampleMs;

    void _updateState();
};
