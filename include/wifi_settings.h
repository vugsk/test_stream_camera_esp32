#ifndef WIFI_SETTINGS_H
#define WIFI_SETTINGS_H

#include <Arduino.h>
#include <Preferences.h>

// Initialize WiFi settings storage
void initWiFiSettings();

// Save WiFi credentials to NVS
void saveWiFiCredentials(const String& ssid, const String& password);

// Load WiFi credentials from NVS
bool loadWiFiCredentials(String& ssid, String& password);

// Get current WiFi SSID
String getCurrentSSID();

// Save server host to NVS
void saveServerHost(const String& host);

// Load server host from NVS
String loadServerHost();

// Get current server host (returns from NVS or default from config.h)
String getCurrentServerHost();

// Check if server host is valid (not empty)
bool isServerHostValid();

#endif // WIFI_SETTINGS_H
