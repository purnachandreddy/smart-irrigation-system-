// =============================================================
//  FlowSensor.cpp  —  Optional water flow sensor module
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#include "FlowSensor.h"

#if USE_FLOW_SENSOR

// Static ISR-shared variable
volatile uint32_t FlowSensor::_pulseCount = 0;

// ── ISR ──────────────────────────────────────────────────────
void IRAM_ATTR FlowSensor::onPulse() {
    _pulseCount++;
}

// ─────────────────────────────────────────────────────────────
FlowSensor::FlowSensor()
    : _pin(0), _lastPulseCount(0), _lastTickMs(0),
      _flowRateMlPerMin(0.0f), _sessionVolumeMl(0.0f), _dailyVolumeMl(0.0f),
      _pumpOnStartMs(0), _pumpIsOn(false), _noFlowFault(false)
{}

void FlowSensor::begin(uint8_t pin) {
    _pin = pin;
    pinMode(_pin, INPUT_PULLUP);
    attachInterrupt(digitalPinToInterrupt(_pin), onPulse, RISING);
    LOG("FlowSensor: started on pin %d", pin);
}

void FlowSensor::tick(uint32_t nowMs) {
    uint32_t elapsed = nowMs - _lastTickMs;
    if (elapsed < 1000) return;  // update at most every second
    _lastTickMs = nowMs;

    // Snapshot pulse count atomically
    noInterrupts();
    uint32_t currentCount = _pulseCount;
    interrupts();

    uint32_t newPulses = currentCount - _lastPulseCount;
    _lastPulseCount = currentCount;

    // mL = pulses / (pulses per litre) * 1000
    float newVolumeMl = (float)newPulses / (float)FLOW_PULSES_PER_LITRE * 1000.0f;

    // Flow rate in mL/min
    _flowRateMlPerMin = newVolumeMl / (float)elapsed * 60000.0f;

    _sessionVolumeMl += newVolumeMl;
    _dailyVolumeMl   += newVolumeMl;

    // ── No-flow fault detection ───────────────────────────────
    if (_pumpIsOn && newPulses == 0) {
        uint32_t pumpOnDuration = nowMs - _pumpOnStartMs;
        if (pumpOnDuration > FLOW_FAULT_TIMEOUT_MS) {
            if (!_noFlowFault) {
                _noFlowFault = true;
                LOG("FlowSensor: NO FLOW FAULT — pump on %lu ms with no pulses", pumpOnDuration);
            }
        }
    } else {
        _noFlowFault = false;
    }
}

float FlowSensor::getFlowRateMlPerMin() const { return _flowRateMlPerMin; }
float FlowSensor::getSessionVolumeMl()  const { return _sessionVolumeMl;  }
float FlowSensor::getDailyVolumeMl()    const { return _dailyVolumeMl;    }
bool  FlowSensor::isNoFlowFault()       const { return _noFlowFault;      }

void FlowSensor::resetSession() {
    _sessionVolumeMl = 0.0f;
    _noFlowFault     = false;
}

void FlowSensor::resetDaily() {
    _dailyVolumeMl = 0.0f;
}

void FlowSensor::notifyPumpOn(uint32_t nowMs) {
    _pumpIsOn      = true;
    _pumpOnStartMs = nowMs;
    _noFlowFault   = false;
}

void FlowSensor::notifyPumpOff() {
    _pumpIsOn    = false;
    _noFlowFault = false;
}

#endif  // USE_FLOW_SENSOR
