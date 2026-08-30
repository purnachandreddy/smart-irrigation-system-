// =============================================================
//  config.h  —  ALL hardware pins, compile flags, and defaults
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#pragma once
#include <cstdint>

// ─────────────────────────────────────────────────────────────
//  HARDWARE PINS
// ─────────────────────────────────────────────────────────────

// Capacitive soil moisture sensor (ADC1 channel — avoid ADC2 with Wi-Fi)
#define SOIL_SENSOR_PIN         34   // GPIO34 — ADC1_CH6  (input only)

// DC pump control via MOSFET / relay
// HIGH = pump ON, LOW = pump OFF
#define PUMP_PIN                26   // GPIO26 — digital output

// Water-level sensor
//   Analog mode  : connect signal to an ADC1 pin (0–3.3 V)
//   Digital mode : float switch — HIGH = water present
#define WATER_LEVEL_PIN         35   // GPIO35 — ADC1_CH7  (input only)

// Optional flow sensor (Hall-effect pulse output)
#define FLOW_SENSOR_PIN         32   // GPIO32 — interrupt capable

// Optional DHT22 temperature/humidity sensor
#define DHT_PIN                 27   // GPIO27

// Built-in LED (status indicator)
#define STATUS_LED_PIN          2    // GPIO2  — on-board LED

// ─────────────────────────────────────────────────────────────
//  FEATURE FLAGS  (also settable via platformio.ini build_flags)
// ─────────────────────────────────────────────────────────────
#ifndef USE_FLOW_SENSOR
#define USE_FLOW_SENSOR    0   // 1 = enable, 0 = disable
#endif

#ifndef USE_DHT
#define USE_DHT            0   // 1 = DHT22 temp/humidity
#endif

#ifndef DEBUG_SERIAL
#define DEBUG_SERIAL       1   // 1 = verbose serial output
#endif

#ifndef WATER_LEVEL_ANALOG
#define WATER_LEVEL_ANALOG 1   // 1 = analog sensor, 0 = digital float switch
#endif

// ─────────────────────────────────────────────────────────────
//  SERIAL DEBUG MACRO
// ─────────────────────────────────────────────────────────────
#if DEBUG_SERIAL
  #define LOG(fmt, ...)  Serial.printf("[%s] " fmt "\n", __func__, ##__VA_ARGS__)
  #define LOGN(msg)      Serial.println(msg)
#else
  #define LOG(fmt, ...)  do {} while(0)
  #define LOGN(msg)      do {} while(0)
#endif

// ─────────────────────────────────────────────────────────────
//  SOIL MOISTURE CALIBRATION DEFAULTS
//  Override via dashboard calibration page; stored in NVS.
// ─────────────────────────────────────────────────────────────
#define DEFAULT_SOIL_DRY_VALUE   2800   // ADC reading in completely dry air/soil
#define DEFAULT_SOIL_WET_VALUE   1100   // ADC reading submerged in water

// EMA filter coefficient: 0.0 (no smoothing) – 1.0 (infinite smoothing)
// 0.15 gives ~7-sample effective window, good for slow moisture changes
#define SOIL_EMA_ALPHA           0.15f

// Consecutive invalid readings before declaring sensor error
#define SOIL_INVALID_COUNT_MAX   5

// ─────────────────────────────────────────────────────────────
//  WATER LEVEL SENSOR THRESHOLDS (analog mode, 0–4095)
// ─────────────────────────────────────────────────────────────
#define WATER_LEVEL_FULL_ADC     3500   // ADC ≥ this → FULL
#define WATER_LEVEL_NORMAL_ADC   2400   // ADC ≥ this → NORMAL
#define WATER_LEVEL_LOW_ADC      1200   // ADC ≥ this → LOW
                                        // ADC < LOW   → EMPTY

// ─────────────────────────────────────────────────────────────
//  FLOW SENSOR  (optional)
// ─────────────────────────────────────────────────────────────
// Pulses per litre (typical YF-S201 = 450 pulses/L)
#define FLOW_PULSES_PER_LITRE    450
// Minimum expected flow when pump is ON (mL/min)
#define FLOW_MIN_EXPECTED_ML_MIN 50
// Duration (ms) to wait before declaring no-flow fault
#define FLOW_FAULT_TIMEOUT_MS    4000

// ─────────────────────────────────────────────────────────────
//  TIMING
// ─────────────────────────────────────────────────────────────
// How often the main sensor read + decision tick runs (ms)
#define SENSOR_SAMPLE_INTERVAL_MS   10000UL   // 10 s

// How often a data point is added to the drying model (ms)
#define PREDICTION_SAMPLE_INTERVAL_MS  300000UL  // 5 min

// How long to wait after a pump pulse for moisture to stabilise (ms)
#define DEFAULT_STABILIZATION_DELAY_MS  30000UL  // 30 s

// How long a single watering pulse lasts (ms)
#define DEFAULT_PULSE_DURATION_MS       4000UL   // 4 s

// Minimum time between two complete watering sessions (ms)
#define DEFAULT_WATERING_COOLDOWN_MS    1800000UL // 30 min

// ─────────────────────────────────────────────────────────────
//  SAFETY LIMITS (hard-coded floor values; profiles can be tighter)
// ─────────────────────────────────────────────────────────────
// Absolute maximum pump runtime in one session (ms)
#define ABSOLUTE_MAX_PUMP_RUNTIME_MS   60000UL   // 60 s total

// Maximum watering pulses per session
#define ABSOLUTE_MAX_WATERING_CYCLES   8

// Maximum daily water volume (mL); hard ceiling
#define ABSOLUTE_MAX_DAILY_WATER_ML    1000

// ─────────────────────────────────────────────────────────────
//  DRYING MODEL
// ─────────────────────────────────────────────────────────────
// Circular buffer size (number of {timestamp, moisture} entries)
#define DRYING_MODEL_BUFFER_SIZE    24

// Minimum number of valid points required before predictions are enabled
#define DRYING_MODEL_MIN_POINTS     3

// Minimum time span in the buffer before predictions are trusted (minutes)
#define DRYING_MODEL_MIN_SPAN_MIN   30

// Drying rate is considered "data unavailable" when variance is very high
#define DRYING_MODEL_MAX_RATE_PCT_HR  20.0f  // sanity cap

// ─────────────────────────────────────────────────────────────
//  STORAGE / LOGGING
// ─────────────────────────────────────────────────────────────
// LittleFS log file path
#define LOG_FILE_PATH          "/events.json"

// Maximum log file size before rotation (bytes)
#define LOG_MAX_FILE_SIZE_BYTES 40000

// Maximum log entries kept in memory for API response
#define LOG_MAX_ENTRIES_MEMORY  100

// NVS namespace
#define NVS_NAMESPACE          "smartplant"

// ─────────────────────────────────────────────────────────────
//  WI-FI / NETWORK
// ─────────────────────────────────────────────────────────────
// AP mode (captive portal) credentials
#define WIFI_AP_SSID           "SmartPlant-Setup"
#define WIFI_AP_PASSWORD       "plantsetup"

// How many STA connection attempts before falling back to AP mode
#define WIFI_STA_MAX_ATTEMPTS  20

// Interval between reconnect attempts when disconnected (ms)
#define WIFI_RECONNECT_INTERVAL_MS  30000UL

// NTP servers
#define NTP_SERVER_1           "pool.ntp.org"
#define NTP_SERVER_2           "time.nist.gov"
#define NTP_GMT_OFFSET_SEC     19800    // UTC+5:30 (adjust for your timezone)
#define NTP_DAYLIGHT_OFFSET_SEC 0

// Web server port
#define WEB_SERVER_PORT        80

// ─────────────────────────────────────────────────────────────
//  FIRMWARE VERSION
// ─────────────────────────────────────────────────────────────
#define FW_VERSION_MAJOR  1
#define FW_VERSION_MINOR  0
#define FW_VERSION_PATCH  0
#define FW_VERSION_STR    "1.0.0"
