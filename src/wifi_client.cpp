#include "wifi_client.h"
#include "wifi_settings.h"
#include "config.h"

static unsigned long lastReconnectAttempt = 0;
static const unsigned long RECONNECT_INTERVAL = 5000;
static String currentSSID;
static String currentPassword;

bool initWiFi() {
  // Load WiFi credentials from storage (or use defaults)
  loadWiFiCredentials(currentSSID, currentPassword);
  
  Serial.println("Connecting to WiFi: " + currentSSID);
  
  WiFi.mode(WIFI_STA);
  WiFi.setSleep(false);  // Отключаем WiFi sleep для минимальных задержек
  WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
  
  int attempts = 0;
  while (WiFi.status() != WL_CONNECTED && attempts < 20) {
    delay(500);
    Serial.print(".");
    attempts++;
  }
  
  if (WiFi.status() == WL_CONNECTED) {
    Serial.println("\nWiFi connected!");
    Serial.println("IP: " + WiFi.localIP().toString());
    
    // Оптимизация TCP для стриминга
    WiFi.setTxPower(WIFI_POWER_19_5dBm);  // Максимальная мощность передачи
    
    return true;
  } else {
    Serial.println("\nWiFi connection FAILED!");
    return false;
  }
}

bool isWiFiConnected() {
  return WiFi.status() == WL_CONNECTED;
}

void checkWiFiConnection() {
  if (WiFi.status() != WL_CONNECTED) {
    unsigned long now = millis();
    if (now - lastReconnectAttempt > RECONNECT_INTERVAL) {
      lastReconnectAttempt = now;
      Serial.println("WiFi lost. Reconnecting...");
      WiFi.disconnect();
      
      // Reload credentials in case they were updated
      loadWiFiCredentials(currentSSID, currentPassword);
      WiFi.begin(currentSSID.c_str(), currentPassword.c_str());
    }
  }
}

String getLocalIP() {
  if (WiFi.status() == WL_CONNECTED) {
    return WiFi.localIP().toString();
  }
  return "Not connected";
}

void disconnectWiFi() {
  Serial.println("Disconnecting WiFi...");
  WiFi.disconnect(true);  // true = очистить сохраненные данные
  WiFi.mode(WIFI_OFF);    // Полностью выключаем WiFi
  delay(100);
  Serial.println("WiFi disconnected and radio off");
}
