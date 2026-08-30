// =============================================================
//  FlowSensor.h  —  Optional water flow sensor module
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
//  Compiled only when USE_FLOW_SENSOR == 1
// =============================================================
#pragma once
#include <Arduino.h>
#include "../config.h"

#if USE_FLOW_SENSOR

// ─────────────────────────────────────────────────────────────
class FlowSensor {
public:
    FlowSensor();

    // Call once in setup()
    void begin(uint8_t pin);

    // Call every loop — updates flow calculations
    void tick(uint32_t nowMs);

    // Flow rate in mL/min (averaged over last measurement window)
    float    getFlowRateMlPerMin() const;

    // Total volume dispensed since last resetSession()
    float    getSessionVolumeMl()  const;

    // Total volume dispensed today (reset at midnight by IrrigationController)
    float    getDailyVolumeMl()    const;

    // True if pump is ON but no pulses detected for FLOW_FAULT_TIMEOUT_MS
    bool     isNoFlowFault()       const;

    void     resetSession();
    void     resetDaily();

    // Called by interrupt
    static void IRAM_ATTR onPulse();

private:
    uint8_t  _pin;

    // Shared with ISR — must be volatile
    static volatile uint32_t _pulseCount;

    uint32_t _lastPulseCount;     // snapshot at last tick
    uint32_t _lastTickMs;

    float    _flowRateMlPerMin;
    float    _sessionVolumeMl;
    float    _dailyVolumeMl;

    // No-flow fault detection
    uint32_t _pumpOnStartMs;
    bool     _pumpIsOn;
    bool     _noFlowFault;

public:
    // Called by IrrigationController to update pump state for fault detection
    void     notifyPumpOn(uint32_t nowMs);
    void     notifyPumpOff();
};

#else  // USE_FLOW_SENSOR == 0

// ─────────────────────────────────────────────────────────────
//  Stub: same interface, all methods no-op or return safe defaults
// ─────────────────────────────────────────────────────────────
class FlowSensor {
public:
    void  begin(uint8_t) {}
    void  tick(uint32_t) {}
    float getFlowRateMlPerMin()  const { return 0.0f; }
    float getSessionVolumeMl()   const { return 0.0f; }
    float getDailyVolumeMl()     const { return 0.0f; }
    bool  isNoFlowFault()        const { return false; }
    void  resetSession()               {}
    void  resetDaily()                 {}
    void  notifyPumpOn(uint32_t)       {}
    void  notifyPumpOff()              {}
};

#endif  // USE_FLOW_SENSOR
