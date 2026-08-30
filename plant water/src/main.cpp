// =============================================================
//  main.cpp  —  ESP32 Smart Plant Irrigation System
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
//
//  Architecture:
//    sensors → IrrigationController (state machine) → pump
//    IrrigationController → DryingModel (prediction)
//    IrrigationController + all → DataLogger (storage)
//    WiFiManager + WebServer (IoT layer — secondary to local control)
//
//  The main loop is non-blocking: millis()-based everywhere.
//  Irrigation decisions run independently of Wi-Fi status.
// =============================================================
#include <Arduino.h>
#include <esp_task_wdt.h>   // Hardware watchdog
#include <time.h>
#include <cstring>

#include "config.h"
#include "sensors/SoilMoisture.h"
#include "sensors/WaterLevel.h"
#include "sensors/FlowSensor.h"
#include "irrigation/PlantProfiles.h"
#include "irrigation/IrrigationController.h"
#include "prediction/DryingModel.h"
#include "storage/DataLogger.h"
#include "network/WiFiManager.h"
#include "web/DashboardServer.h"

// ─────────────────────────────────────────────────────────────
//  Module instances (global, all default-constructed here)
// ─────────────────────────────────────────────────────────────
SoilMoisture         g_soil;
WaterLevel           g_level;
FlowSensor           g_flow;
IrrigationController g_ctrl;
DryingModel          g_model;
DataLogger           g_logger;
WiFiManager          g_wifi;
DashboardServer      g_web;

// Active plant profile pointer — set from NVS, may change at runtime
const PlantProfile*  g_activeProfile = &PROFILE_NORMAL;

// User configuration (loaded from NVS)
UserConfig           g_cfg;

// Custom profile definition (data stored here, populated from NVS)
// Custom profile — initialized in setup() after config is loaded
PlantProfile PROFILE_CUSTOM = {
    .id                     = "custom",
    .name                   = "Custom",
    .emoji                  = "\xE2\x9A\x99\xEF\xB8\x8F",
    .criticalDryness        = 25.0f,
    .minMoisture            = 35.0f,
    .targetMoisture         = 55.0f,
    .maxMoisture            = 70.0f,
    .pulseDurationMs        = 4000,
    .stabilizationDelayMs   = 30000,
    .maxPulsesPerSession    = 6,
    .wateringCooldownMs     = 1800000UL,
    .maxPumpRuntimeMs       = 40000,
    .maxDailyWaterMl        = 600,
    .dryingRateWarnThreshold= 1.5f,
};

// ─────────────────────────────────────────────────────────────
//  Watchdog timeout — 30 seconds
// ─────────────────────────────────────────────────────────────
#define WDT_TIMEOUT_SEC  30

// ─────────────────────────────────────────────────────────────
//  Midnight reset tracker (for daily water counter)
// ─────────────────────────────────────────────────────────────
static uint8_t g_lastDay = 255;

void checkMidnightReset() {
    struct tm ti;
    if (!getLocalTime(&ti, 0)) return;
    if (ti.tm_mday != g_lastDay) {
        g_lastDay = ti.tm_mday;
        g_ctrl.resetDailyWater();
        Serial.printf("[%lu] Midnight reset — daily water counter cleared\n", millis());
    }
}

// ─────────────────────────────────────────────────────────────
//  setup()
// ─────────────────────────────────────────────────────────────
void setup() {
    Serial.begin(115200);
    delay(500);  // Allow serial monitor to connect

    Serial.println("\n\n========================================");
    Serial.printf("  Smart Plant Irrigation v%s\n", FW_VERSION_STR);
    Serial.println("========================================\n");

    // ── 1. Hardware watchdog ──────────────────────────────────
    // ESP32 Arduino core 3.x uses esp_task_wdt_reconfigure();
    // core 2.x uses esp_task_wdt_init(uint32_t, bool).
    // The #if below handles both.
#if ESP_ARDUINO_VERSION >= ESP_ARDUINO_VERSION_VAL(3, 0, 0)
    {
        const esp_task_wdt_config_t wdt_cfg = {
            .timeout_ms    = WDT_TIMEOUT_SEC * 1000,
            .idle_core_mask = 0,
            .trigger_panic  = true
        };
        esp_task_wdt_reconfigure(&wdt_cfg);
    }
#else
    esp_task_wdt_init(WDT_TIMEOUT_SEC, true);
#endif
    esp_task_wdt_add(nullptr);  // Subscribe current task

    // ── 2. Storage: LittleFS + NVS ────────────────────────────
    g_logger.begin();
    g_logger.loadConfig(g_cfg);

    // ── 3. Resolve active plant profile ───────────────────────
    g_activeProfile = g_logger.resolveActiveProfile(g_cfg);

    // Populate custom profile fields from config
    PROFILE_CUSTOM.minMoisture          = g_cfg.customMinMoisture;
    PROFILE_CUSTOM.targetMoisture       = g_cfg.customTargetMoisture;
    PROFILE_CUSTOM.maxMoisture          = g_cfg.customMaxMoisture;
    PROFILE_CUSTOM.criticalDryness      = g_cfg.customCriticalDryness;
    PROFILE_CUSTOM.pulseDurationMs      = g_cfg.customPulseDurationMs;
    PROFILE_CUSTOM.stabilizationDelayMs = g_cfg.customStabilizationDelayMs;
    PROFILE_CUSTOM.wateringCooldownMs   = g_cfg.customWateringCooldownMs;
    PROFILE_CUSTOM.maxDailyWaterMl      = g_cfg.customMaxDailyWaterMl;
    PROFILE_CUSTOM.maxPulsesPerSession  = g_cfg.customMaxPulsesPerSession;

    Serial.printf("[Setup] Active profile: %s\n", g_activeProfile->name);

    // ── 4. Sensors ────────────────────────────────────────────
    g_soil.begin(SOIL_SENSOR_PIN, g_cfg.soilDryValue, g_cfg.soilWetValue);
    g_level.begin(WATER_LEVEL_PIN);

#if USE_FLOW_SENSOR
    g_flow.begin(FLOW_SENSOR_PIN);
#endif

    // Force initial sensor reads before controller starts
    g_soil.tick(0);
    g_level.tick(0);

    // ── 5. Irrigation controller ──────────────────────────────
    g_ctrl.setSoilSensor (&g_soil);
    g_ctrl.setWaterLevel (&g_level);
    g_ctrl.setFlowSensor (&g_flow);
    g_ctrl.setDryingModel(&g_model);
    g_ctrl.setDataLogger (&g_logger);
    g_ctrl.begin(g_activeProfile);
    g_ctrl.enableAutoWatering(g_cfg.autoWateringEnabled);
    g_ctrl.enableAdaptive(g_cfg.adaptivePredictionEnabled);

    // ── 6. Wi-Fi (non-blocking — irrigation continues regardless) ─
    g_wifi.begin(g_cfg.wifiSSID, g_cfg.wifiPassword);

    // ── 7. Web server ─────────────────────────────────────────
    g_web.setSoilSensor   (&g_soil);
    g_web.setWaterLevel   (&g_level);
    g_web.setFlowSensor   (&g_flow);
    g_web.setController   (&g_ctrl);
    g_web.setDryingModel  (&g_model);
    g_web.setDataLogger   (&g_logger);
    g_web.setWiFiManager  (&g_wifi);
    g_web.setUserConfig   (&g_cfg);
    g_web.setActiveProfile(&g_activeProfile);
    g_web.begin();

    // ── 8. Serial status banner ───────────────────────────────
    Serial.println("[Setup] All modules initialised");
    Serial.printf("[Setup] WiFi: %s\n", g_wifi.getModeStr());
    Serial.printf("[Setup] Soil: %.1f%% [%s]\n",
        g_soil.getPercentage(), g_soil.getStateStr());
    Serial.printf("[Setup] Tank: %s\n", g_level.getStateStr());
    Serial.println("[Setup] Entering main loop...\n");
}

// ─────────────────────────────────────────────────────────────
//  loop()  — non-blocking, millis()-based
// ─────────────────────────────────────────────────────────────
void loop() {
    uint32_t nowMs = millis();

    // ── Feed hardware watchdog ────────────────────────────────
    esp_task_wdt_reset();

    // ── Tick all modules ──────────────────────────────────────
    g_soil.tick(nowMs);
    g_level.tick(nowMs);

#if USE_FLOW_SENSOR
    g_flow.tick(nowMs);
#endif

    // ── Core irrigation state machine ─────────────────────────
    // Runs locally, completely independent of Wi-Fi
    g_ctrl.tick(nowMs);

    // ── Network ───────────────────────────────────────────────
    g_wifi.tick(nowMs);

    // If captive portal just received new Wi-Fi credentials → save them
    if (g_wifi.hasNewCredentials()) {
        strncpy(g_cfg.wifiSSID,     g_wifi.getPendingSSID(),     sizeof(g_cfg.wifiSSID) - 1);
        strncpy(g_cfg.wifiPassword, g_wifi.getPendingPassword(), sizeof(g_cfg.wifiPassword) - 1);
        g_logger.saveConfig(g_cfg);
        g_wifi.clearNewCredentials();
    }

    // ── Periodic data logging ─────────────────────────────────
    g_logger.periodicWrite(nowMs);

    // ── Midnight daily-water reset ────────────────────────────
    if (g_wifi.isTimeSynced()) {
        checkMidnightReset();
    }

    // ── Debug serial print (every 10 s when DEBUG_SERIAL=1) ──
#if DEBUG_SERIAL
    static uint32_t lastDebugMs = 0;
    if ((nowMs - lastDebugMs) >= 10000UL) {
        lastDebugMs = nowMs;

        float moisture = g_soil.getPercentage();
        float rate     = g_model.getDryingRatePctPerHour();

        Serial.printf("\n[%lu] ─────────────────────────────\n", nowMs);
        Serial.printf("[%lu] Soil moisture  : %.1f%%  [%s]\n",
            nowMs, moisture, g_soil.getStateStr());

        if (!isnan(rate)) {
            Serial.printf("[%lu] Drying rate    : %.2f %%/hr\n", nowMs, rate);
        } else {
            Serial.printf("[%lu] Drying rate    : %s\n", nowMs, g_model.getStatusStr());
        }

        float minsLeft = g_ctrl.getMinutesUntilWater();
        if (!isnan(minsLeft)) {
            Serial.printf("[%lu] Time to water  : %dh %dm\n",
                nowMs, (int)(minsLeft/60), (int)minsLeft % 60);
        } else {
            Serial.printf("[%lu] Time to water  : unknown\n", nowMs);
        }

        Serial.printf("[%lu] Plant profile  : %s\n", nowMs, g_activeProfile->name);
        Serial.printf("[%lu] State          : %s\n", nowMs, g_ctrl.getStateStr());
        Serial.printf("[%lu] Plant status   : %s %s\n",
            nowMs, g_ctrl.getStatusEmoji(), g_ctrl.getPlantStatusStr());
        Serial.printf("[%lu] Tank           : %s (%.0f%%)\n",
            nowMs, g_level.getStateStr(), g_level.getPercentage());
        Serial.printf("[%lu] Pump           : %s\n",
            nowMs, g_ctrl.isPumpOn() ? "ON" : "OFF");
        Serial.printf("[%lu] Daily water    : %.0f mL\n",
            nowMs, g_ctrl.getDailyWaterMl());
        Serial.printf("[%lu] WiFi           : %s (%s)\n",
            nowMs, g_wifi.getModeStr(), g_wifi.getLocalIP().c_str());

        if (strlen(g_ctrl.getLastError()) > 0) {
            Serial.printf("[%lu] Last error     : %s\n", nowMs, g_ctrl.getLastError());
        }
        Serial.printf("[%lu] ─────────────────────────────\n\n", nowMs);
    }
#endif

    // Yield to FreeRTOS / Wi-Fi tasks — avoids watchdog on tight loops
    yield();
}
