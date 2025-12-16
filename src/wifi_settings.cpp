#include "wifi_settings.h"
#include "config.h"
#include <WiFi.h>

static Preferences preferences;

void initWiFiSettings() {
  preferences.begin("wifi", false);
  Serial.println("WiFi settings storage initialized");
}

void saveWiFiCredentials(const String& ssid, const String& password) {
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
  Serial.println("WiFi credentials saved to NVS");
}

bool loadWiFiCredentials(String& ssid, String& password) {
  ssid = preferences.getString("ssid", WIFI_SSID);
  password = preferences.getString("password", WIFI_PASSWORD);
  
  // Return true if we have stored credentials
  return preferences.isKey("ssid");
}

String getCurrentSSID() {
  return preferences.getString("ssid", WIFI_SSID);
}
