// =============================================================
//  DryingModel.cpp  —  Soil drying rate estimation & prediction
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#include "DryingModel.h"
#include <math.h>

// How long after a watering event to suppress new readings (ms)
// Let moisture distribute fully before resuming trend tracking
static constexpr uint32_t POST_WATERING_SUPPRESS_MS = 600000UL;  // 10 minutes

// ─────────────────────────────────────────────────────────────
DryingModel::DryingModel()
    : _head(0), _count(0),
      _lastWateringMs(0), _postWateringCooldown(false),
      _cachedRatePctPerHour(0.0f), _cacheValid(false)
{
    memset(_buffer, 0, sizeof(_buffer));
}

// ─────────────────────────────────────────────────────────────
void DryingModel::addReading(uint32_t timestampMs, float moisture) {
    // Skip readings immediately after watering — moisture is rising,
    // which would corrupt the drying trend
    if (_postWateringCooldown) {
        uint32_t elapsed = timestampMs - _lastWateringMs;
        if (elapsed < POST_WATERING_SUPPRESS_MS) {
            LOG("DryingModel: skipping post-watering reading (%.1f%%)", moisture);
            return;
        }
        _postWateringCooldown = false;
    }

    // Write into circular buffer
    _buffer[_head] = { timestampMs, moisture, true };
    _head = (_head + 1) % DRYING_MODEL_BUFFER_SIZE;
    if (_count < DRYING_MODEL_BUFFER_SIZE) _count++;

    _cacheValid = false;  // Invalidate cached regression

    LOG("DryingModel: added reading %.1f%% (n=%d)", moisture, _count);
}

// ─────────────────────────────────────────────────────────────
void DryingModel::notifyWatering(uint32_t timestampMs) {
    _lastWateringMs       = timestampMs;
    _postWateringCooldown = true;
    _cacheValid           = false;
    LOG("DryingModel: watering event noted — suppressing readings for 10 min");
}

// ─────────────────────────────────────────────────────────────
bool DryingModel::isDataSufficient() const {
    if (_count < DRYING_MODEL_MIN_POINTS) return false;

    // Find oldest and newest valid readings
    uint32_t oldest = UINT32_MAX, newest = 0;
    for (uint8_t i = 0; i < DRYING_MODEL_BUFFER_SIZE; i++) {
        if (!_buffer[i].valid) continue;
        if (_buffer[i].timestampMs < oldest) oldest = _buffer[i].timestampMs;
        if (_buffer[i].timestampMs > newest) newest = _buffer[i].timestampMs;
    }
    if (oldest == UINT32_MAX) return false;

    // Need at least DRYING_MODEL_MIN_SPAN_MIN of data
    uint32_t spanMs  = newest - oldest;
    uint32_t spanMin = spanMs / 60000UL;
    return (spanMin >= DRYING_MODEL_MIN_SPAN_MIN);
}

// ─────────────────────────────────────────────────────────────
float DryingModel::getDryingRatePctPerHour() const {
    if (!isDataSufficient()) return NAN;

    if (!_cacheValid) {
        _cachedRatePctPerHour = _computeOLSSlope();
        _cacheValid = true;
    }

    // Sanity clamp: reject physically impossible rates
    float r = _cachedRatePctPerHour;
    if (r > DRYING_MODEL_MAX_RATE_PCT_HR) {
        LOG("DryingModel: rate %.2f capped at %.1f", r, DRYING_MODEL_MAX_RATE_PCT_HR);
        return DRYING_MODEL_MAX_RATE_PCT_HR;
    }

    return r;
}

// ─────────────────────────────────────────────────────────────
float DryingModel::getMinutesUntilCritical(float currentMoisture, float criticalPct) const {
    float rate = getDryingRatePctPerHour();
    if (isnan(rate) || rate <= 0.0f) return NAN;  // no data or gaining moisture

    float moistureGap = currentMoisture - criticalPct;
    if (moistureGap <= 0.0f) return 0.0f;  // already critical

    // timeUntilCritical = moistureGap / rate (in hours) → convert to minutes
    return (moistureGap / rate) * 60.0f;
}

// ─────────────────────────────────────────────────────────────
const char* DryingModel::getStatusStr() const {
    if (_count == 0)              return "No data yet";
    if (!isDataSufficient())      return "Collecting data...";
    float rate = getDryingRatePctPerHour();
    if (isnan(rate))              return "Prediction unavailable";
    if (rate < 0.0f)              return "Soil gaining moisture";
    return "Active prediction";
}

// ─────────────────────────────────────────────────────────────
void DryingModel::reset() {
    memset(_buffer, 0, sizeof(_buffer));
    _head       = 0;
    _count      = 0;
    _cacheValid = false;
    _postWateringCooldown = false;
    LOG("DryingModel: buffer cleared");
}

uint8_t DryingModel::getCount() const { return _count; }

// ─────────────────────────────────────────────────────────────
//  Ordinary Least Squares linear regression
//  X = time in hours since oldest reading
//  Y = moisture percentage
//  Returns the negative slope (drying rate): positive = getting drier
// ─────────────────────────────────────────────────────────────
float DryingModel::_computeOLSSlope() const {
    // Collect valid points
    float xArr[DRYING_MODEL_BUFFER_SIZE];
    float yArr[DRYING_MODEL_BUFFER_SIZE];
    uint8_t n = 0;

    // Find the oldest timestamp to use as X origin
    uint32_t t0 = UINT32_MAX;
    for (uint8_t i = 0; i < DRYING_MODEL_BUFFER_SIZE; i++) {
        if (_buffer[i].valid && _buffer[i].timestampMs < t0)
            t0 = _buffer[i].timestampMs;
    }

    for (uint8_t i = 0; i < DRYING_MODEL_BUFFER_SIZE; i++) {
        if (!_buffer[i].valid) continue;
        // Convert ms elapsed to hours
        xArr[n] = (float)(_buffer[i].timestampMs - t0) / 3600000.0f;
        yArr[n] = _buffer[i].moisture;
        n++;
    }

    if (n < 2) return 0.0f;

    // OLS: slope = (n*Σxy - Σx*Σy) / (n*Σx² - (Σx)²)
    float sumX = 0.0f, sumY = 0.0f, sumXY = 0.0f, sumX2 = 0.0f;
    for (uint8_t i = 0; i < n; i++) {
        sumX  += xArr[i];
        sumY  += yArr[i];
        sumXY += xArr[i] * yArr[i];
        sumX2 += xArr[i] * xArr[i];
    }

    float denom = (float)n * sumX2 - sumX * sumX;
    if (fabsf(denom) < 1e-6f) return 0.0f;  // flat trend

    float slope = ((float)n * sumXY - sumX * sumY) / denom;

    // slope is negative when moisture is falling (drying)
    // we return dryingRate as positive for "getting drier"
    float dryingRate = -slope;

    LOG("DryingModel: OLS slope=%.4f  dryingRate=%.4f %%/hr (n=%d)", slope, dryingRate, n);
    return dryingRate;
}
