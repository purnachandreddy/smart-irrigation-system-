// =============================================================
//  PlantProfiles.h  —  Configurable plant profiles
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#pragma once
#include <stdint.h>
#include "../config.h"

// ─────────────────────────────────────────────────────────────
//  Plant Profile Structure
// ─────────────────────────────────────────────────────────────
struct PlantProfile {
    const char* id;               // Short identifier stored in NVS
    const char* name;             // Human-readable name
    const char* emoji;            // Dashboard icon

    // Moisture thresholds (percent, 0–100)
    float criticalDryness;        // Emergency level — water immediately
    float minMoisture;            // Lower acceptable bound
    float targetMoisture;         // Ideal moisture after watering
    float maxMoisture;            // Upper safe bound (overwatering guard)

    // Watering pulse behaviour
    uint32_t pulseDurationMs;     // Single pump-ON duration (ms)
    uint32_t stabilizationDelayMs;// Wait after pulse for moisture to equalise (ms)
    uint8_t  maxPulsesPerSession; // Max pulses in one watering session

    // Session limits
    uint32_t wateringCooldownMs;  // Minimum gap between sessions (ms)
    uint32_t maxPumpRuntimeMs;    // Total pump ON time per session (ms)
    uint16_t maxDailyWaterMl;     // Daily water budget (mL)

    // Prediction sensitivity
    float dryingRateWarnThreshold; // %/hr above which WATER_SOON is triggered early
};

// ─────────────────────────────────────────────────────────────
//  Built-in profiles
// ─────────────────────────────────────────────────────────────

// Cactus / succulents: drought-tolerant, infrequent watering
static const PlantProfile PROFILE_CACTUS = {
    .id                     = "cactus",
    .name                   = "Cactus",
    .emoji                  = "\xF0\x9F\x8C\xB5",  // 🌵
    .criticalDryness        = 15.0f,
    .minMoisture            = 20.0f,
    .targetMoisture         = 35.0f,
    .maxMoisture            = 50.0f,
    .pulseDurationMs        = 2000,        // 2 s pulses
    .stabilizationDelayMs   = 60000,       // 60 s settle
    .maxPulsesPerSession    = 4,
    .wateringCooldownMs     = 7200000UL,   // 2 hours
    .maxPumpRuntimeMs       = 20000,       // 20 s total
    .maxDailyWaterMl        = 200,
    .dryingRateWarnThreshold= 0.3f,        // very slow drying expected
};

// Normal houseplant
static const PlantProfile PROFILE_NORMAL = {
    .id                     = "normal",
    .name                   = "Normal Plant",
    .emoji                  = "\xF0\x9F\x8C\xB1",  // 🌱
    .criticalDryness        = 25.0f,
    .minMoisture            = 35.0f,
    .targetMoisture         = 55.0f,
    .maxMoisture            = 70.0f,
    .pulseDurationMs        = 4000,        // 4 s pulses
    .stabilizationDelayMs   = 30000,       // 30 s settle
    .maxPulsesPerSession    = 6,
    .wateringCooldownMs     = 1800000UL,   // 30 min
    .maxPumpRuntimeMs       = 40000,       // 40 s total
    .maxDailyWaterMl        = 600,
    .dryingRateWarnThreshold= 1.5f,
};

// Moisture-loving plants: ferns, peace lily, tropical plants
static const PlantProfile PROFILE_MOISTURE_LOVING = {
    .id                     = "moisture_loving",
    .name                   = "Moisture-Loving",
    .emoji                  = "\xF0\x9F\x8C\xBF",  // 🌿
    .criticalDryness        = 35.0f,
    .minMoisture            = 50.0f,
    .targetMoisture         = 70.0f,
    .maxMoisture            = 85.0f,
    .pulseDurationMs        = 5000,        // 5 s pulses
    .stabilizationDelayMs   = 20000,       // 20 s settle
    .maxPulsesPerSession    = 8,
    .wateringCooldownMs     = 900000UL,    // 15 min
    .maxPumpRuntimeMs       = 60000,       // 60 s total
    .maxDailyWaterMl        = 1000,
    .dryingRateWarnThreshold= 2.5f,
};

// User-customised profile (populated from NVS at runtime)
// Defaults to NORMAL; modified via dashboard
extern PlantProfile PROFILE_CUSTOM;

// ─────────────────────────────────────────────────────────────
//  Profile registry (used for enumeration in dashboard)
// ─────────────────────────────────────────────────────────────
static const PlantProfile* BUILT_IN_PROFILES[] = {
    &PROFILE_CACTUS,
    &PROFILE_NORMAL,
    &PROFILE_MOISTURE_LOVING,
};
static const uint8_t NUM_BUILT_IN_PROFILES = 3;
