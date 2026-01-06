#include "wifi_settings.h"
#include "config.h"
#include <WiFi.h>

static Preferences preferences;

void initWiFiSettings() {
  preferences.begin("wifi", false);
}

void saveWiFiCredentials(const String& ssid, const String& password) {
  preferences.putString("ssid", ssid);
  preferences.putString("password", password);
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

void saveServerHost(const String& host) {
  preferences.putString("server_host", host);
}

String loadServerHost() {
  return preferences.getString("server_host", SERVER_HOST);
}

String getCurrentServerHost() {
  String host = preferences.getString("server_host", "");
  if (host.length() == 0) {
    host = SERVER_HOST;
  }
  return host;
}

bool isServerHostValid() {
  String host = getCurrentServerHost();
  return host.length() > 0;
}
