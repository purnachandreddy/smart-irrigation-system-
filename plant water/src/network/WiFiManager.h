// =============================================================
//  WiFiManager.h  —  Non-blocking Wi-Fi + AP captive portal
//  Smart Plant Irrigation System  |  ESP32  |  v1.0
// =============================================================
#pragma once
#include <Arduino.h>
#include <WiFi.h>
#include "../config.h"

// ─────────────────────────────────────────────────────────────
enum class WiFiMode : uint8_t {
    DISCONNECTED,   // Not yet connected; attempting STA
    CONNECTING,     // STA connection in progress
    CONNECTED,      // STA connected successfully
    AP_MODE,        // Running as access point (setup portal)
    AP_STA,         // AP open AND STA connected (brief overlap)
};

// ─────────────────────────────────────────────────────────────
class WiFiManager {
public:
    WiFiManager();

    // Call in setup() — provide credentials saved in NVS.
    // Leave ssid empty to go straight to AP mode.
    void begin(const char* ssid, const char* password);

    // Non-blocking tick — call every loop() iteration
    void tick(uint32_t nowMs);

    // ── State queries ─────────────────────────────────────────
    WiFiMode    getMode()          const { return _mode; }
    bool        isConnected()      const { return _mode == WiFiMode::CONNECTED; }
    bool        isAPMode()         const { return _mode == WiFiMode::AP_MODE; }
    const char* getModeStr()       const;
    String      getLocalIP()       const;
    String      getSSID()          const;
    int8_t      getRSSI()          const;

    // True if NTP has been synced at least once
    bool        isTimeSynced()     const { return _timeSynced; }

    // ── Credential management ─────────────────────────────────
    // Called by the captive portal handler when user submits credentials
    void        setCredentials(const char* ssid, const char* password);

    // Access credentials stored in this instance (for saving to NVS)
    const char* getPendingSSID()     const { return _ssid; }
    const char* getPendingPassword() const { return _password; }
    bool        hasNewCredentials()  const { return _newCredentials; }
    void        clearNewCredentials()      { _newCredentials = false; }

    // Force reconnect (e.g. after profile change)
    void        reconnect();

private:
    WiFiMode    _mode;
    char        _ssid[64];
    char        _password[64];
    bool        _newCredentials;

    uint8_t     _connectAttempts;
    uint32_t    _lastAttemptMs;
    uint32_t    _lastReconnectMs;
    bool        _timeSynced;

    void _startSTA();
    void _startAP();
    void _syncNTP();
};
