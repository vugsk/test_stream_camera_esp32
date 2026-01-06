#include "bluetooth_config.h"
#include "wifi_settings.h"
#include <BluetoothSerial.h>
#include <ArduinoJson.h>

BluetoothSerial SerialBT;

static bool btActive = false;
static bool newCredentialsReceived = false;
static String receivedSSID = "";
static String receivedPassword = "";
static String receivedServerHost = "";
static String btDeviceName = "ESP32-CAM-Config";

void initBluetoothConfig() {
  // Load Bluetooth name from preferences if available
  Preferences prefs;
  prefs.begin("bluetooth", true);
  String savedName = prefs.getString("name", "");
  prefs.end();
  
  if (savedName.length() > 0) {
    btDeviceName = savedName;
  }
}

void startBluetoothConfig() {
  if (btActive) return;
  
  Serial.println("Starting Bluetooth for WiFi configuration...");
  
  if (SerialBT.begin(btDeviceName)) {
    btActive = true;
    newCredentialsReceived = false;
    Serial.println("Bluetooth started: " + btDeviceName);
  } else {
    Serial.println("Bluetooth start FAILED!");
  }
}

void stopBluetoothConfig() {
  if (!btActive) return;
  
  SerialBT.end();
  btActive = false;
  Serial.println("Bluetooth stopped");
}

bool isBluetoothConfigActive() {
  return btActive;
}

bool hasNewWiFiCredentials() {
  return newCredentialsReceived;
}

bool getReceivedCredentials(String& ssid, String& password) {
  if (!newCredentialsReceived) return false;
  
  ssid = receivedSSID;
  password = receivedPassword;
  newCredentialsReceived = false;
  
  return true;
}


// Для совместимости с интерфейсом
bool hasNewServerHost() { return false; }
String getReceivedServerHost() { return ""; }

void handleBluetoothConfig() {
  if (!btActive) return;
  
  // Check if data available
  if (SerialBT.available()) {
    String received = SerialBT.readStringUntil('\n');
    received.trim();
    
    if (received.length() > 0) {
      // Try to parse JSON: {"ssid":"network","password":"pass","server_host":"192.168.0.107"}
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, received);
      if (!error) {
        if (doc["ssid"].is<const char*>() && doc["password"].is<const char*>()) {
          receivedSSID = doc["ssid"].as<String>();
          receivedPassword = doc["password"].as<String>();
          
          // Получаем адрес сервера (опционально)
          if (doc["server_host"].is<const char*>()) {
            receivedServerHost = doc["server_host"].as<String>();
          }
          
          if (receivedSSID.length() > 0) {
            newCredentialsReceived = true;
            Serial.println("BT: WiFi credentials received");
            
            // Send confirmation
            SerialBT.println("{\"status\":\"ok\",\"message\":\"Credentials received\"}");
            // Save credentials
            saveWiFiCredentials(receivedSSID, receivedPassword);
            
            // Save server host if provided
            if (receivedServerHost.length() > 0) {
              saveServerHost(receivedServerHost);
              Serial.println("Server host saved: " + receivedServerHost);
            }
          }
        }
      } else {
        // Try simple format: SSID,PASSWORD,SERVER_HOST (server_host is optional)
        int firstComma = received.indexOf(',');
        if (firstComma > 0) {
          receivedSSID = received.substring(0, firstComma);
          
          int secondComma = received.indexOf(',', firstComma + 1);
          if (secondComma > 0) {
            // Format with server host: SSID,PASSWORD,SERVER_HOST
            receivedPassword = received.substring(firstComma + 1, secondComma);
            receivedServerHost = received.substring(secondComma + 1);
            receivedServerHost.trim();
          } else {
            // Format without server host: SSID,PASSWORD
            receivedPassword = received.substring(firstComma + 1);
            receivedServerHost = "";
          }
          
          receivedSSID.trim();
          receivedPassword.trim();
          
          if (receivedSSID.length() > 0) {
            newCredentialsReceived = true;
            Serial.println("BT: Credentials received");
            
            SerialBT.println("OK");
            saveWiFiCredentials(receivedSSID, receivedPassword);
            
            // Save server host if provided
            if (receivedServerHost.length() > 0) {
              saveServerHost(receivedServerHost);
              Serial.println("Server host saved: " + receivedServerHost);
            }
          }
        }
      }
    }
  }
}
