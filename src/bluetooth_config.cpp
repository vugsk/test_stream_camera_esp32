#include "bluetooth_config.h"
#include "wifi_settings.h"
#include <BluetoothSerial.h>
#include <ArduinoJson.h>

BluetoothSerial SerialBT;

static bool btActive = false;
static bool newCredentialsReceived = false;
static String receivedSSID = "";
static String receivedPassword = "";
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
    Serial.println("Waiting for WiFi credentials from mobile app...");
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

void handleBluetoothConfig() {
  if (!btActive) return;
  
  // Check if data available
  if (SerialBT.available()) {
    String received = SerialBT.readStringUntil('\n');
    received.trim();
    
    if (received.length() > 0) {
      Serial.println("Received BT data: " + received);
      
      // Try to parse JSON: {"ssid":"network","password":"pass"}
      JsonDocument doc;
      DeserializationError error = deserializeJson(doc, received);
      
      if (!error) {
        if (doc["ssid"].is<const char*>() && doc["password"].is<const char*>()) {
          receivedSSID = doc["ssid"].as<String>();
          receivedPassword = doc["password"].as<String>();
          
          if (receivedSSID.length() > 0) {
            newCredentialsReceived = true;
            Serial.println("WiFi credentials received via Bluetooth");
            Serial.println("SSID: " + receivedSSID);
            
            // Send confirmation
            SerialBT.println("{\"status\":\"ok\",\"message\":\"Credentials received\"}");
            
            // Save credentials
            saveWiFiCredentials(receivedSSID, receivedPassword);
          }
        }
      } else {
        // Try simple format: SSID,PASSWORD
        int commaIndex = received.indexOf(',');
        if (commaIndex > 0) {
          receivedSSID = received.substring(0, commaIndex);
          receivedPassword = received.substring(commaIndex + 1);
          
          receivedSSID.trim();
          receivedPassword.trim();
          
          if (receivedSSID.length() > 0) {
            newCredentialsReceived = true;
            Serial.println("WiFi credentials received (simple format)");
            Serial.println("SSID: " + receivedSSID);
            
            SerialBT.println("OK");
            saveWiFiCredentials(receivedSSID, receivedPassword);
          }
        }
      }
    }
  }
}
