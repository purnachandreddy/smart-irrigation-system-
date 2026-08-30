// =============================================================
//  WiFiManager.cpp  —  Non-blocking Wi-Fi + AP captive portal
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#include "WiFiManager.h"
#include <esp_wifi.h>
#include <time.h>

// ─────────────────────────────────────────────────────────────
WiFiManager::WiFiManager()
    : _mode(WiFiMode::DISCONNECTED),
      _newCredentials(false),
      _connectAttempts(0),
      _lastAttemptMs(0),
      _lastReconnectMs(0),
      _timeSynced(false)
{
    memset(_ssid,     0, sizeof(_ssid));
    memset(_password, 0, sizeof(_password));
}

// ─────────────────────────────────────────────────────────────
void WiFiManager::begin(const char* ssid, const char* password) {
    strncpy(_ssid,     ssid     ? ssid     : "", sizeof(_ssid) - 1);
    strncpy(_password, password ? password : "", sizeof(_password) - 1);

    WiFi.mode(WIFI_STA);
    WiFi.setAutoReconnect(false);   // We manage reconnection ourselves

    if (strlen(_ssid) > 0) {
        LOG("WiFiManager: attempting STA connection to '%s'", _ssid);
        _startSTA();
    } else {
        LOG("WiFiManager: no SSID stored — starting AP mode");
        _startAP();
    }
}

// ─────────────────────────────────────────────────────────────
void WiFiManager::tick(uint32_t nowMs) {
    switch (_mode) {
        // ── Actively connecting ───────────────────────────────
        case WiFiMode::CONNECTING: {
            wl_status_t status = WiFi.status();

            if (status == WL_CONNECTED) {
                _mode = WiFiMode::CONNECTED;
                _connectAttempts = 0;
                Serial.printf("[WiFi] Connected! IP: %s  RSSI: %d dBm\n",
                    WiFi.localIP().toString().c_str(), WiFi.RSSI());
                _syncNTP();
                return;
            }

            // Timeout: 500 ms per attempt
            if ((nowMs - _lastAttemptMs) < 500) return;
            _lastAttemptMs = nowMs;
            _connectAttempts++;

            if (_connectAttempts >= WIFI_STA_MAX_ATTEMPTS) {
                Serial.printf("[WiFi] STA failed after %d attempts — falling back to AP\n",
                    _connectAttempts);
                WiFi.disconnect(true);
                _startAP();
            }
            break;
        }

        // ── Connected — check for drops ───────────────────────
        case WiFiMode::CONNECTED: {
            if (WiFi.status() != WL_CONNECTED) {
                Serial.println("[WiFi] Connection lost — will retry");
                _mode = WiFiMode::DISCONNECTED;
                _lastReconnectMs = nowMs;
            }
            // Periodic NTP re-sync (once per hour)
            static uint32_t lastNTPSync = 0;
            if (!_timeSynced || (nowMs - lastNTPSync) > 3600000UL) {
                lastNTPSync = nowMs;
                _syncNTP();
            }
            break;
        }

        // ── Disconnected — periodic reconnect ─────────────────
        case WiFiMode::DISCONNECTED: {
            if ((nowMs - _lastReconnectMs) >= WIFI_RECONNECT_INTERVAL_MS) {
                _lastReconnectMs = nowMs;
                if (strlen(_ssid) > 0) {
                    LOG("WiFiManager: retrying STA connection");
                    _startSTA();
                }
            }
            break;
        }

        // ── AP mode — just keep serving, nothing to do ────────
        case WiFiMode::AP_MODE:
        case WiFiMode::AP_STA:
            break;
    }
}

// ─────────────────────────────────────────────────────────────
void WiFiManager::setCredentials(const char* ssid, const char* password) {
    strncpy(_ssid,     ssid,     sizeof(_ssid) - 1);
    strncpy(_password, password, sizeof(_password) - 1);
    _newCredentials  = true;
    _connectAttempts = 0;
    Serial.printf("[WiFi] New credentials received for '%s'\n", ssid);
    // Transition: try STA while keeping AP alive briefly
    _mode = WiFiMode::DISCONNECTED;
    _startSTA();
}

// ─────────────────────────────────────────────────────────────
void WiFiManager::reconnect() {
    WiFi.disconnect(true);
    _mode = WiFiMode::DISCONNECTED;
    _connectAttempts = 0;
    if (strlen(_ssid) > 0) _startSTA();
}

// ─────────────────────────────────────────────────────────────
//  Private helpers
// ─────────────────────────────────────────────────────────────
void WiFiManager::_startSTA() {
    WiFi.mode(WIFI_STA);
    WiFi.begin(_ssid, _password);
    _mode            = WiFiMode::CONNECTING;
    _connectAttempts = 0;
    _lastAttemptMs   = millis();
    LOG("WiFiManager: STA begin '%s'", _ssid);
}

void WiFiManager::_startAP() {
    WiFi.mode(WIFI_AP);
    WiFi.softAP(WIFI_AP_SSID, WIFI_AP_PASSWORD);
    _mode = WiFiMode::AP_MODE;
    Serial.printf("[WiFi] AP mode — SSID: %s  IP: %s\n",
        WIFI_AP_SSID, WiFi.softAPIP().toString().c_str());
}

void WiFiManager::_syncNTP() {
    if (!isConnected()) return;
    configTime(NTP_GMT_OFFSET_SEC, NTP_DAYLIGHT_OFFSET_SEC,
               NTP_SERVER_1, NTP_SERVER_2);

    // Wait briefly for sync (max 2 s non-blocking approximation)
    struct tm ti;
    uint32_t t0 = millis();
    while (!getLocalTime(&ti, 0) && (millis() - t0) < 2000) {
        delay(100);
    }

    if (getLocalTime(&ti, 0)) {
        _timeSynced = true;
        Serial.printf("[WiFi] NTP synced — %04d-%02d-%02d %02d:%02d:%02d\n",
            ti.tm_year + 1900, ti.tm_mon + 1, ti.tm_mday,
            ti.tm_hour, ti.tm_min, ti.tm_sec);
    } else {
        LOG("WiFiManager: NTP sync failed");
    }
}

// ─────────────────────────────────────────────────────────────
const char* WiFiManager::getModeStr() const {
    switch (_mode) {
        case WiFiMode::DISCONNECTED: return "Disconnected";
        case WiFiMode::CONNECTING:   return "Connecting";
        case WiFiMode::CONNECTED:    return "Connected";
        case WiFiMode::AP_MODE:      return "Setup (AP)";
        case WiFiMode::AP_STA:       return "AP+STA";
        default:                     return "Unknown";
    }
}

String WiFiManager::getLocalIP() const {
    if (_mode == WiFiMode::CONNECTED) return WiFi.localIP().toString();
    if (_mode == WiFiMode::AP_MODE)   return WiFi.softAPIP().toString();
    return "—";
}

String WiFiManager::getSSID() const {
    if (_mode == WiFiMode::CONNECTED) return WiFi.SSID();
    if (_mode == WiFiMode::AP_MODE)   return String(WIFI_AP_SSID);
    return "—";
}

int8_t WiFiManager::getRSSI() const {
    if (_mode == WiFiMode::CONNECTED) return WiFi.RSSI();
    return 0;
}
