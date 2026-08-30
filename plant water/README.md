# 🌱 Adaptive IoT Smart Plant Irrigation System

An ESP32-based adaptive plant watering controller that goes beyond simple threshold switching. Instead of turning the pump on when moisture is low, the system tracks moisture history, estimates the soil drying rate using linear regression, predicts when watering will be needed, delivers water in controlled pulses, verifies the result, and adapts its behaviour over time.

---

## Table of Contents

1. [Hardware](#1-hardware)
2. [Wiring & Pin Configuration](#2-wiring--pin-configuration)
3. [Required Libraries](#3-required-libraries)
4. [Setup & Flashing](#4-setup--flashing)
5. [Sensor Calibration](#5-sensor-calibration)
6. [The Adaptive Algorithm](#6-the-adaptive-algorithm)
7. [State Machine](#7-state-machine)
8. [Dashboard](#8-dashboard)
9. [Plant Profiles](#9-plant-profiles)
10. [Configuration Reference](#10-configuration-reference)
11. [Troubleshooting](#11-troubleshooting)
12. [Test Procedures](#12-test-procedures)
13. [Software Architecture](#13-software-architecture)

---

## 1. Hardware

| Component | Notes |
|---|---|
| ESP32 development board | Any 38-pin or 30-pin variant |
| Capacitive soil moisture sensor | Preferred over resistive (more durable) |
| DC water pump | 3V–6V mini submersible or 5V USB pump |
| MOSFET module (or relay) | Logic-level MOSFET (e.g. IRLZ44N) or 5V relay |
| Water tank | Any container ≥ 500 mL |
| Water level sensor | Analog resistive or digital float switch |
| 5 V power supply (external) | Power the pump separately; do NOT power through the ESP32 |
| Flyback diode | 1N4007 across pump terminals if using MOSFET |
| *(Optional)* Water flow sensor | YF-S201 Hall effect sensor |
| *(Optional)* DHT22 sensor | Temperature and humidity |
| Jumper wires, breadboard | Standard |

> **⚠️ Important:** The ESP32 GPIO pins can supply only ~12 mA. Always drive the pump through a MOSFET or relay — never directly from a GPIO.

---

## 2. Wiring & Pin Configuration

All pins are defined in [`src/config.h`](src/config.h) and can be changed there.

```
ESP32 Pin   Component               Direction   Notes
─────────────────────────────────────────────────────────────
GPIO 34     Soil moisture sensor    INPUT       ADC1_CH6 — input-only pin
GPIO 35     Water level sensor      INPUT       ADC1_CH7 — input-only pin
GPIO 26     Pump MOSFET gate        OUTPUT      HIGH = pump ON
GPIO 32     Flow sensor signal      INPUT       Interrupt-capable
GPIO 27     DHT22 data              INPUT/OUT   Optional
GPIO  2     Status LED              OUTPUT      Built-in LED
GND         All sensor grounds      —           Common ground
3.3 V       Sensors VCC             —           Do NOT power pump from here
```

### Pump Circuit (MOSFET)

```
ESP32 GPIO 26 ──── 100 Ω ──┬─── MOSFET Gate
                            │
                           10 kΩ
                            │
                           GND

MOSFET Drain ──── Pump (-)
MOSFET Source ─── GND
5 V external ──── Pump (+)

Flyback diode across pump terminals:
  Pump (+) ──── Cathode (band) of 1N4007
  Pump (-)  ─── Anode of 1N4007
```

### Soil Moisture Sensor Wiring

```
Sensor VCC  ──── 3.3 V
Sensor GND  ──── GND
Sensor AOUT ──── GPIO 34
```

### Water Level Sensor (Analog)

```
Sensor VCC  ──── 3.3 V
Sensor GND  ──── GND
Sensor AOUT ──── GPIO 35
```

For a **digital float switch**: connect one terminal to GPIO 35 and the other to GND. Enable `INPUT_PULLUP` internally by setting `WATER_LEVEL_ANALOG=0` in `platformio.ini`.

---

## 3. Required Libraries

Managed automatically by PlatformIO via `platformio.ini`:

| Library | Purpose |
|---|---|
| `ESPAsyncWebServer` | Non-blocking HTTP server |
| `AsyncTCP` | Required by ESPAsyncWebServer |
| `ArduinoJson` ≥ v7 | JSON serialisation for REST API |
| `DHT sensor library` | DHT22 support (only when `USE_DHT=1`) |
| `Adafruit Unified Sensor` | DHT22 dependency |
| `LittleFS` | Built-in to ESP32 Arduino core ≥ 2.x |
| `Preferences` | Built-in NVS wrapper |

---

## 4. Setup & Flashing

### Prerequisites

- [VS Code](https://code.visualstudio.com/) + [PlatformIO IDE extension](https://platformio.org/install/ide?install=vscode)
- USB cable (data, not charge-only)

### Steps

```bash
# 1. Clone / open the project folder in VS Code
#    PlatformIO automatically detects platformio.ini

# 2. Adjust hardware pins in src/config.h if needed

# 3. Build and upload firmware
#    PlatformIO Toolbar → ✓ Build → → Upload

# 4. Upload the web dashboard to LittleFS
#    PlatformIO Toolbar → Upload Filesystem Image
#    (or: pio run --target uploadfs)

# 5. Open Serial Monitor at 115200 baud
#    Watch boot output and sensor readings

# 6. Connect to the 'SmartPlant-Setup' Wi-Fi access point
#    Password: plantsetup
#    Open browser → http://192.168.4.1
#    Enter your home Wi-Fi credentials and submit

# 7. After connection, find the ESP32's IP in serial monitor
#    Open http://<ESP32-IP> for the full dashboard
```

> **Note:** If you reflash only the firmware (not the filesystem), your LittleFS files are preserved. A full flash erase will remove both firmware and filesystem, clearing NVS configuration too.

---

## 5. Sensor Calibration

The system converts the raw ADC value (0–4095) to a moisture percentage. Because every sensor-soil combination is slightly different, you must calibrate once for accurate readings.

### Why Calibration Matters

A sensor in air reads a high ADC value (dry). Submerged in water it reads low. The range varies between sensors and changes with soil type.

### Calibration Procedure

**Method A — Dashboard (recommended):**

1. Open the dashboard → *Configuration* → *Soil Sensor Calibration*
2. **Step 1 — Dry value:**
   - Hold the sensor in open air (or completely dry soil)
   - Click **Sample Current**
   - Record the raw ADC value shown
   - Enter it in the *Dry ADC value* field
3. **Step 2 — Wet value:**
   - Submerge the sensor tip in water
   - Click **Sample Current**
   - Record the raw ADC value
   - Enter it in the *Wet ADC value* field
4. Click **Apply Calibration** — values are saved to NVS immediately

**Method B — Serial Monitor:**

Observe the line: `SoilMoisture: raw=XXXX  unfiltered=Y.Y%  filtered=Z.Z%`

Note the raw values in dry and wet conditions and enter them in `config.h`:

```cpp
#define DEFAULT_SOIL_DRY_VALUE   2800   // your dry reading
#define DEFAULT_SOIL_WET_VALUE   1100   // your wet reading
```

### EMA Filtering

Raw ADC readings are noisy. The system applies an Exponential Moving Average (EMA):

```
filtered = (α × new_reading) + ((1 − α) × previous_filtered)
```

`α = 0.15` (default) gives an effective window of ~7 samples. This smooths transient noise while still tracking genuine moisture changes. Adjust `SOIL_EMA_ALPHA` in `config.h` if needed.

---

## 6. The Adaptive Algorithm

This is the core innovation versus a simple threshold controller.

### 6.1 Moisture History Collection

Every 5 minutes (configurable via `PREDICTION_SAMPLE_INTERVAL_MS`), the system stores a `{timestamp, moisture}` reading in a circular buffer (24 entries = 2 hours of data at default rate).

Readings taken immediately after watering are suppressed for 10 minutes so the rising moisture phase doesn't corrupt the drying trend.

### 6.2 Drying Rate Estimation

Instead of computing rate from just two consecutive readings (fragile), the system fits an **Ordinary Least Squares (OLS) linear regression** to all buffered readings:

```
X = elapsed time in hours (since oldest buffered reading)
Y = moisture percentage

OLS slope = (n·ΣXY − ΣX·ΣY) / (n·ΣX² − (ΣX)²)

dryingRate = −slope   (positive = soil getting drier)
```

Example with 4 readings:

```
08:00 → 70%
12:00 → 62%
16:00 → 54%
20:00 → 47%

OLS slope ≈ −2.05 %/hr
dryingRate ≈  2.05 %/hr
```

Requires at least 3 readings spanning ≥ 30 minutes before predictions are enabled. Until then the dashboard shows *"Collecting data…"* and the system falls back to immediate-threshold watering.

### 6.3 Time-to-Critical Prediction

```
timeUntilCritical = (currentMoisture − criticalMoisture) / dryingRate

Example:
  current = 50%,  critical = 40%,  dryingRate = 2.0 %/hr
  timeUntilCritical = (50 − 40) / 2.0 = 5 hours
```

### 6.4 Watering Decision Logic

The decision is **not** `if (moisture < threshold) → pump on`. It considers:

| Factor | Weight |
|---|---|
| Current moisture vs. profile thresholds | Primary |
| Drying rate trend | Secondary |
| Predicted time until critical | Secondary |
| Time since last watering (cooldown) | Safety |
| Daily water budget | Safety |
| Tank level | Safety |
| Sensor validity | Safety |

**Plant statuses and when to water:**

| Status | Condition | Action |
|---|---|---|
| Healthy | moisture ≥ target | None |
| Drying | moisture < target, slow rate | None yet |
| Water Soon | <2 h predicted until min | Monitor closely |
| Needs Water | moisture ≤ minMoisture | Water immediately |
| Critical | moisture ≤ criticalDryness | Water immediately (urgent) |

Proactive watering triggers if prediction shows <30 minutes until critical.

### 6.5 Pulse-Based Water Delivery

Rather than running the pump for a fixed time:

```
1. Calculate moisture deficit: target − current
2. Start pump for pulseDuration (default 4 s)
3. Turn pump OFF
4. Wait stabilizationDelay (default 30 s) for water to distribute
5. Read moisture sensor again
6. If moisture ≥ target → done
7. If moisture < target → repeat from step 2
8. After maxPulsesPerSession → stop regardless
```

This prevents overwatering from a single stuck-open valve and handles soils with slow water propagation.

### 6.6 Watering Verification

After every pulse, the system checks whether moisture increased:

- **Rise > 1%** → healthy, continue
- **No rise after 2 consecutive pulses** → fault detected → pump error state

---

## 7. State Machine

```
STARTUP
  │  (sensor warm-up, 3 ticks)
  ▼
MONITORING  ◄──────────────────────────────────────────┐
  │                                                    │
  ├─ soil valid AND moisture healthy ─► MONITORING     │
  ├─ moisture < target, slow rate   ─► DRYING          │
  ├─ predicted <2 h to min         ─► WATER_SOON       │
  ├─ needs water OR critical        ─► WATERING ───────┤
  ├─ tank empty                     ─► LOW_WATER       │
  └─ sensor invalid                 ─► SENSOR_ERROR    │
                                                       │
WATERING (pump pulse fires)                            │
  │  pulse duration elapsed                            │
  ▼                                                    │
STABILIZING (water distributes)                        │
  │  stabilizationDelay elapsed                        │
  ▼                                                    │
VERIFYING (read moisture)                              │
  ├─ moisture ≥ target              ─► WATERING_COMPLETE ─┘
  ├─ moisture < target, can pulse   ─► WATERING (loop)
  ├─ max pulses reached             ─► WATERING_COMPLETE
  └─ no moisture rise (fault)       ─► PUMP_ERROR

PUMP_ERROR (pump off, log fault)
  └─ 3 consecutive failures         ─► CRITICAL_ERROR

CRITICAL_ERROR (everything off, stays until user resets)

SENSOR_ERROR (pump off)
  └─ sensor recovers                ─► MONITORING

LOW_WATER (watering disabled)
  └─ tank has water again           ─► MONITORING

CALIBRATING (user mode, pump off)
  └─ exitCalibrationMode()          ─► STARTUP
```

---

## 8. Dashboard

The web dashboard is a single-page application served from LittleFS on the ESP32 itself. No internet connection is required to view it — only a connection to the ESP32's IP address (or to the `SmartPlant-Setup` AP during initial setup).

### Accessing the Dashboard

- **After Wi-Fi setup:** `http://<ESP32-IP>/`  (IP shown in serial monitor)
- **During initial setup:** Connect to `SmartPlant-Setup` → `http://192.168.4.1/`

### Features

| Panel | Shows |
|---|---|
| Moisture gauge | Animated SVG arc, colour-coded by status |
| Threshold bar | Min / Target / Max markers on gradient bar |
| Prediction panel | Drying rate (%/hr), time until water, sparkline chart |
| Tank | Animated fill, percentage, state (FULL/LOW/EMPTY) |
| Pump | Animated pulse ring when active, daily water total, last watered |
| Profile chips | Select Cactus / Normal / Moisture-Loving / Custom |
| Watering history | Last 50 events in sortable table |
| Calibration | Sample raw ADC, set dry/wet values |
| Wi-Fi settings | Enter new SSID/password via captive portal form |
| System info | State, IP, RSSI, uptime, firmware version |

### REST API Endpoints

| Method | Path | Description |
|---|---|---|
| GET | `/api/status` | Full system snapshot (JSON) |
| GET | `/api/history?n=50` | Last N log entries |
| GET | `/api/config` | Current configuration |
| POST | `/api/config` | Update configuration |
| POST | `/api/profile` | Change plant profile |
| POST | `/api/water` | Trigger manual watering |
| POST | `/api/stop` | Emergency pump stop |
| POST | `/api/calibrate` | Soil sensor calibration |
| POST | `/api/wifi` | Save Wi-Fi credentials |
| POST | `/api/reset-history` | Clear log and drying model |

---

## 9. Plant Profiles

| Parameter | Cactus 🌵 | Normal 🌱 | Moisture-Loving 🌿 |
|---|---|---|---|
| Critical dryness | 15% | 25% | 35% |
| Min moisture | 20% | 35% | 50% |
| Target moisture | 35% | 55% | 70% |
| Max moisture | 50% | 70% | 85% |
| Pulse duration | 2 s | 4 s | 5 s |
| Stabilisation delay | 60 s | 30 s | 20 s |
| Max pulses/session | 4 | 6 | 8 |
| Cooldown between sessions | 2 h | 30 min | 15 min |
| Max pump runtime/session | 20 s | 40 s | 60 s |
| Max daily water | 200 mL | 600 mL | 1000 mL |

**Custom profile:** Select *Custom* in the dashboard and submit new threshold values via the Config panel. The system saves the values to NVS so they survive reboots.

---

## 10. Configuration Reference

All defaults live in `src/config.h`. NVS-stored values override these after first save.

```cpp
// Pin assignments
#define SOIL_SENSOR_PIN      34
#define PUMP_PIN             26
#define WATER_LEVEL_PIN      35
#define FLOW_SENSOR_PIN      32
#define DHT_PIN              27
#define STATUS_LED_PIN        2

// Calibration defaults
#define DEFAULT_SOIL_DRY_VALUE   2800
#define DEFAULT_SOIL_WET_VALUE   1100
#define SOIL_EMA_ALPHA           0.15f

// Timing
#define SENSOR_SAMPLE_INTERVAL_MS        10000   // 10 s
#define PREDICTION_SAMPLE_INTERVAL_MS   300000   // 5 min
#define DEFAULT_STABILIZATION_DELAY_MS   30000   // 30 s
#define DEFAULT_PULSE_DURATION_MS         4000   // 4 s
#define DEFAULT_WATERING_COOLDOWN_MS   1800000   // 30 min

// Safety ceilings (all profiles respect these)
#define ABSOLUTE_MAX_PUMP_RUNTIME_MS    60000    // 60 s
#define ABSOLUTE_MAX_WATERING_CYCLES        8
#define ABSOLUTE_MAX_DAILY_WATER_ML      1000

// Wi-Fi
#define WIFI_AP_SSID         "SmartPlant-Setup"
#define WIFI_AP_PASSWORD     "plantsetup"
#define WIFI_STA_MAX_ATTEMPTS     20
#define NTP_GMT_OFFSET_SEC      19800   // UTC+5:30 — change for your timezone
```

---

## 11. Troubleshooting

### "Soil sensor invalid" / SENSOR_ERROR state

- Check wiring: VCC → 3.3 V, GND → GND, signal → GPIO 34
- Avoid ADC2 pins (GPIO 0, 2, 4, 12–15, 25–27) — they conflict with Wi-Fi
- Run calibration — if raw ADC reads 0 or 4095 constantly, the pin is floating
- Try a different ADC1 pin (32, 33, 34, 35, 36, 39)

### Pump turns on but moisture does not rise

- Verify the water tube is in the soil, not pointing out of the pot
- Check tube for blockages
- Ensure pump is actually running (listen for sound/vibration)
- Increase `pulseDurationMs` in the profile
- Increase `stabilizationDelayMs` — water may need longer to reach the sensor

### Dashboard not loading

1. Confirm LittleFS was uploaded (`pio run --target uploadfs`)
2. Check the correct partition scheme is selected in `platformio.ini`
3. Try `http://<IP>/` not `https://`

### Wi-Fi not connecting

- SSID is case-sensitive
- WPA3-only networks may need ESP32 Arduino core update
- Use 2.4 GHz band — ESP32 does not support 5 GHz
- The device falls back to AP mode after 20 failed attempts

### Watering triggers too frequently

- Increase `wateringCooldownMs` in the profile or config panel
- Reduce `dryingRateWarnThreshold` — the trigger sensitivity
- Ensure calibration is correct (incorrect wet value causes readings to appear lower than reality)

### Memory / crash issues

- Use PlatformIO's `pio run --target size` to check flash usage
- Reduce `LOG_MAX_ENTRIES_MEMORY` if RAM is low
- Reduce `DRYING_MODEL_BUFFER_SIZE` if needed (minimum 5 for useful predictions)

---

## 12. Test Procedures

Run each scenario and observe serial output + dashboard.

| Test | Setup | Expected Result |
|---|---|---|
| T1 Normal moisture | Sensor at 60% (above target) | State: MONITORING, Status: Healthy, no watering |
| T2 Gradual drying | Simulate moisture drop across 5+ readings | Drying rate appears on dashboard, status → DRYING |
| T3 Below threshold | Moisture drops below minMoisture | Status → NEEDS_WATER, watering session starts |
| T4 Pulse raises moisture to target | First pulse adds enough water | Session completes after 1 pulse, WATERING_COMPLETE |
| T5 First pulse insufficient | First pulse not enough | Second pulse fires, moisture checked again |
| T6 Empty tank | Set tank ADC below WATER_LEVEL_LOW_ADC | State → LOW_WATER, pump does NOT start |
| T7 Pump on, no flow | `USE_FLOW_SENSOR=1`, block tube | PUMP_ERROR after `FLOW_FAULT_TIMEOUT_MS` |
| T8 Sensor failure | Disconnect soil sensor | State → SENSOR_ERROR, watering disabled |
| T9 Wi-Fi disconnected | Disable AP/router | Serial log continues, watering still occurs locally |
| T10 Daily limit reached | Water until `maxDailyWaterMl` | Auto watering disabled, message in dashboard |
| T11 ESP32 reboot | Power cycle | Pump stays OFF until STARTUP completes (≥3 ticks) |
| T12 Manual watering | Click "Water Now" on dashboard | Safety limits still respected, session starts normally |

---

## 13. Software Architecture

```
smart-plant-irrigation/
├── platformio.ini           ← Build config, library deps, filesystem type
├── README.md
├── data/                    ← LittleFS: uploaded to flash, served over HTTP
│   ├── index.html           ← Dashboard SPA shell
│   ├── style.css            ← Dark-mode premium CSS
│   └── app.js               ← Vanilla JS: API polling, chart, controls
└── src/
    ├── main.cpp             ← setup() / loop() — wires all modules together
    ├── config.h             ← ALL pins, flags, timing, safety limits
    ├── sensors/
    │   ├── SoilMoisture.h/cpp   ← ADC, EMA filter, calibration, validation
    │   ├── WaterLevel.h/cpp     ← Tank level (analog or float switch)
    │   └── FlowSensor.h/cpp     ← ISR pulse counter (optional)
    ├── irrigation/
    │   ├── PlantProfiles.h      ← Cactus/Normal/Moisture-Loving/Custom structs
    │   └── IrrigationController.h/cpp  ← Full state machine, pump control
    ├── prediction/
    │   └── DryingModel.h/cpp    ← Circular buffer, OLS regression, prediction
    ├── storage/
    │   └── DataLogger.h/cpp     ← LittleFS JSONL log, NVS config, ring buffer
    ├── network/
    │   └── WiFiManager.h/cpp    ← Non-blocking STA, AP captive portal, NTP
    └── web/
        └── WebServer.h/cpp      ← ESPAsyncWebServer, all REST API routes
```

### Design Principles

- **Local-first:** The irrigation state machine runs entirely in `loop()`. Wi-Fi and the web server are secondary — a disconnected network never stops watering.
- **Non-blocking:** All timing uses `millis()`. No `delay()` calls in the control path. Only `delayMicroseconds(200)` in the ADC multi-sample loop.
- **Safety by default:** Pump starts `LOW` in `begin()` before any other code runs. Pump is forced `LOW` in every error state.
- **Configurable, not magic numbers:** All thresholds, durations, and limits are in `config.h` or plant profiles. Nothing is scattered.
- **Wear-aware storage:** The log is only written to LittleFS on important events or once per minute during normal operation. NVS writes happen only on explicit config changes.
