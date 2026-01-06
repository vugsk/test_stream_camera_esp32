/*
 * ESP32-CAM Video Stream Client
 * 
 * Отправляет видео поток с камеры на веб-сервер по HTTP POST
 * 
 * Модули:
 *   - config.h             : Настройки (Wi-Fi, пины камеры, сервер)
 *   - camera.h/cpp         : Работа с камерой
 *   - wifi_client.h/cpp    : WiFi подключение
 *   - stream_client.h/cpp  : Стриминг на сервер
 *   - server_settings.h/cpp: Получение настроек с сервера
 *   - sd_recorder.h/cpp    : Запись видео на SD карту
 */

#include <Arduino.h>
#include "config.h"
#include "camera.h"
#include "wifi_client.h"
#include "wifi_settings.h"
#include "bluetooth_config.h"
#include "stream_client.h"
#include "server_settings.h"
#include "sd_recorder.h"
#include "esp_wifi.h"

// Connection state machine
enum ConnectionState {
  STATE_INIT,
  STATE_WIFI_CONNECTING,
  STATE_WIFI_CONNECTED,
  STATE_BLUETOOTH_WAITING,
  STATE_WIFI_RETRY
};

static ConnectionState connectionState = STATE_INIT;
static unsigned long stateStartTime = 0;
static int wifiRetryCount = 0;
static const int MAX_WIFI_RETRIES = 3;
static const unsigned long BLUETOOTH_TIMEOUT = 300000;  // 5 min waiting for BT config

void setup() {
  Serial.begin(115200);
  delay(1000);
  Serial.println("\n=== ESP32-CAM Video Stream Client (HD 60FPS Mode) ===");
  
  // Optimize CPU frequency for maximum performance
  setCpuFrequencyMhz(240);
  Serial.printf("CPU Frequency: %d MHz\n", getCpuFrequencyMhz());
  
  // 1. Initialize camera first
  if (!initCamera()) {
    Serial.println("CRITICAL: Camera init FAILED!");
    while (1) { delay(1000); }
  }
  delay(500);
  
  // 2. Initialize WiFi settings storage
  initWiFiSettings();
  
  // 3. Initialize Bluetooth config
  initBluetoothConfig();
  
  // 4. Initialize streaming
  initStreaming();
  
  // 5. Initialize server settings (loads camera settings from NVS)
  initServerSettings();
  
  // 6. Initialize SD card recorder (loads settings from NVS automatically)
  initSDRecorder();
  
  // 7. НЕ применяем настройки из NVS здесь - они будут загружены с сервера при первом подключении
  // applyCameraSettings(getCurrentSettings());  // <-- Удалено
  
  // 8. Start connection state machine
  connectionState = STATE_INIT;
  stateStartTime = millis();
  
  Serial.println("=== Setup complete ===\n");
}

void loop() {
  // Primary task: send video frames (highest priority)
  updateStreaming();
  
  // WiFi management - НЕ вызываем если Bluetooth активен (конфликт радиомодуля!)
  if (connectionState != STATE_BLUETOOTH_WAITING) {
    checkWiFiConnection();
  }
  
  // handleConnectionStateMachine() {
  unsigned long now = millis();
  String ssid, password;
  
  switch (connectionState) {
    case STATE_INIT:
      // Check if we have server host configuration
      if (!isServerHostValid()) {
        Serial.println("No server host found in NVS or config.h, starting Bluetooth...");
        connectionState = STATE_BLUETOOTH_WAITING;
        stateStartTime = now;
        startBluetoothConfig();
        break;
      }
      
      // Check if we have saved WiFi credentials
      if (loadWiFiCredentials(ssid, password) && ssid.length() > 0) {
        connectionState = STATE_WIFI_CONNECTING;
        wifiRetryCount = 0;
        stateStartTime = now;
        
        if (initWiFi()) {
          connectionState = STATE_WIFI_CONNECTED;
          esp_wifi_set_ps(WIFI_PS_NONE);
        } else {
          Serial.println("Found saved WiFi credentials and server host, attempting connection...");
          connectionState = STATE_WIFI_CONNECTING;
          wifiRetryCount = 0;
          stateStartTime = now;
          
          if (initWiFi()) {
            connectionState = STATE_WIFI_CONNECTED;
            esp_wifi_set_ps(WIFI_PS_NONE);
            Serial.println("WiFi connected successfully!");
          } else {
            connectionState = STATE_WIFI_RETRY;
            stateStartTime = now;
          }
        }
      } else {
        Serial.println("No WiFi credentials found, starting Bluetooth...");
        connectionState = STATE_BLUETOOTH_WAITING;
        stateStartTime = now;
        startBluetoothConfig();
      }
      break;
      
    case STATE_WIFI_RETRY:
      if (now - stateStartTime > 5000) {  // Wait 5 sec before retry
        wifiRetryCount++;
        if (wifiRetryCount >= MAX_WIFI_RETRIES) {
          Serial.println("WiFi connection failed after retries, starting Bluetooth...");
          connectionState = STATE_BLUETOOTH_WAITING;
          stateStartTime = now;
          wifiRetryCount = 0;
          startBluetoothConfig();
        } else {
          if (initWiFi()) {
            connectionState = STATE_WIFI_CONNECTED;
            esp_wifi_set_ps(WIFI_PS_NONE);
          } else {
            stateStartTime = now;
          }
        }
      }
      break;
      
    case STATE_BLUETOOTH_WAITING:
      handleBluetoothConfig();
      
      // Check if new credentials received
      if (hasNewWiFiCredentials()) {
        if (getReceivedCredentials(ssid, password)) {
          Serial.println("New WiFi credentials received, connecting...");
          
          // Check if server host was also received
          if (hasNewServerHost()) {
            String newHost = getReceivedServerHost();
            setServerHost(newHost);
          }
          
          stopBluetoothConfig();
          
          connectionState = STATE_WIFI_CONNECTING;
          stateStartTime = now;
          
          if (initWiFi()) {
            connectionState = STATE_WIFI_CONNECTED;
            esp_wifi_set_ps(WIFI_PS_NONE);
          } else {
            connectionState = STATE_WIFI_RETRY;
            wifiRetryCount = 0;
            stateStartTime = now;
          }
        }
      }
      
      // Timeout check
      if (now - stateStartTime > BLUETOOTH_TIMEOUT) {
        Serial.println("Bluetooth config timeout, retrying WiFi...");
        stopBluetoothConfig();
        connectionState = STATE_WIFI_RETRY;
        wifiRetryCount = 0;
        stateStartTime = now;
      }
      break;
      
    case STATE_WIFI_CONNECTED:
      // Check if WiFi is still connected
      if (!isWiFiConnected()) {
        Serial.println("WiFi connection lost!");
        connectionState = STATE_WIFI_RETRY;
        wifiRetryCount = 0;
        stateStartTime = now;
      } else if (hasServerConnectionError()) {
        // Too many server connection failures - switch to Bluetooth for reconfiguration
        Serial.println("Too many server connection errors! Switching to Bluetooth mode for reconfiguration...");
        resetServerConnectionErrors();
        stopStreaming();
        disconnectWiFi();
        connectionState = STATE_BLUETOOTH_WAITING;
        stateStartTime = now;
        startBluetoothConfig();
      } else {
        // Normal operation - check connection periodically
        checkWiFiConnection();
        
        // First-time: fetch settings from server before starting stream
        if (!areInitialSettingsLoaded()) {
          if (fetchInitialSettingsFromServer()) {
            // Apply loaded settings
            applyCameraSettings(getCurrentSettings());
            delay(500);  // Give camera time to adjust
            // Now start streaming
            startStreaming();
          } else {
            // Still start streaming with defaults
            startStreaming();
          }
        }
        
        // Auto-start streaming if not running
        if (!isStreaming()) {
          startStreaming();
        }
        
        // Handle server settings (неблокирующий, со своим таймером)
        handleServerSettings();
        
        // Send status periodically (неблокирующий, со своим таймером)
        sendStatusToServer();
        
        // Handle SD card hot-plug and recording (неблокирующий)
        handleSDRecorder();
      }
      break;
  }
}

