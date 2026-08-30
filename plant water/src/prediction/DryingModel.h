// =============================================================
//  DryingModel.h  —  Soil drying rate estimation & prediction
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#pragma once
#include <Arduino.h>
#include "../config.h"

// ─────────────────────────────────────────────────────────────
//  One data point in the moisture history
// ─────────────────────────────────────────────────────────────
struct MoistureReading {
    uint32_t timestampMs;   // millis() at time of reading
    float    moisture;      // Percentage 0–100
    bool     valid;         // true = this slot contains data
};

// ─────────────────────────────────────────────────────────────
//  DryingModel
//  Stores a circular buffer of periodic moisture readings and
//  fits a linear regression (least squares) to estimate the
//  current drying rate (% / hour) and time until critical level.
// ─────────────────────────────────────────────────────────────
class DryingModel {
public:
    DryingModel();

    // ── Data ingestion ────────────────────────────────────────

    // Call at prediction sample interval (e.g. every 5 min)
    // Ignores readings taken immediately after watering events
    // to avoid polluting the drying trend with moisture rises.
    void addReading(uint32_t timestampMs, float moisture);

    // Notify model that a watering event just happened.
    // The model will skip readings for a cooldown window after this.
    void notifyWatering(uint32_t timestampMs);

    // ── Predictions ───────────────────────────────────────────

    // Returns drying rate in % per hour (positive = getting drier).
    // Returns NAN if insufficient data.
    float getDryingRatePctPerHour() const;

    // Returns estimated minutes until moisture reaches criticalPct.
    // Returns NAN if insufficient data or drying rate is near zero/negative.
    float getMinutesUntilCritical(float currentMoisture, float criticalPct) const;

    // True when there are enough data points to make reliable predictions.
    bool isDataSufficient() const;

    // Human-readable status string for dashboard
    const char* getStatusStr() const;

    // ── Buffer management ─────────────────────────────────────
    void reset();

    uint8_t getCount()     const;
    const MoistureReading* getBuffer() const { return _buffer; }

private:
    MoistureReading _buffer[DRYING_MODEL_BUFFER_SIZE];
    uint8_t         _head;      // next write position (circular)
    uint8_t         _count;     // number of valid entries

    uint32_t        _lastWateringMs;  // timestamp of last watering event
    bool            _postWateringCooldown;  // skip readings after watering

    // Cached regression results (updated in addReading)
    mutable float   _cachedRatePctPerHour;
    mutable bool    _cacheValid;

    // ── Internal helpers ──────────────────────────────────────

    // Compute slope via ordinary least squares on buffered points.
    // X = elapsed time in hours, Y = moisture percentage
    // Returns positive slope when moisture is decreasing (drying)
    // i.e., we negate the OLS slope: dryingRate = -slope
    float _computeOLSSlope() const;
};
