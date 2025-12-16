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

#endif // WIFI_SETTINGS_H
