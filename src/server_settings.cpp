#include "server_settings.h"
#include "config.h"
#include "camera.h"
#include "wifi_client.h"
#include "wifi_settings.h"
#include "stream_client.h"
#include "sd_recorder.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include <Preferences.h>

static unsigned long lastPollTime = 0;
static unsigned long pollInterval = 10000;  // 10 sec - реже проверяем настройки чтобы не мешать стримингу
static unsigned long lastStatusTime = 0;
static unsigned long statusInterval = 30000;  // 30 sec - отправка статуса
static Preferences btPrefs;
static Preferences cameraPrefs;
static bool initialSettingsLoaded = false;  // Флаг первой загрузки настроек
static bool settingsBusy = false;  // Флаг для предотвращения одновременных HTTP запросов

// Cached URLs to avoid String operations in loop
static String settingsURL;
static String statusURL;
static bool urlsCached = false;

static CameraSettings currentSettings = {
  .frameSize = FRAMESIZE_VGA,    // 640x480
  .quality = STREAM_QUALITY,
  .brightness = 0,
  .contrast = 0,
  .saturation = 0,
  .fps = STREAM_FPS,
  .vflip = false,
  .hmirror = false,
  .streaming = true
};

void loadCameraSettings() {
  cameraPrefs.begin("camera", true);  // Read-only mode
  
  // Load settings from NVS, or use defaults from config.h
  currentSettings.frameSize = cameraPrefs.getInt("frameSize", FRAMESIZE_VGA);
  currentSettings.quality = cameraPrefs.getInt("quality", STREAM_QUALITY);
  currentSettings.brightness = cameraPrefs.getInt("brightness", 0);
  currentSettings.contrast = cameraPrefs.getInt("contrast", 0);
  currentSettings.saturation = cameraPrefs.getInt("saturation", 0);
  currentSettings.fps = cameraPrefs.getInt("fps", STREAM_FPS);
  currentSettings.vflip = cameraPrefs.getBool("vflip", false);
  currentSettings.hmirror = cameraPrefs.getBool("hmirror", false);
  currentSettings.streaming = cameraPrefs.getBool("streaming", true);
  
  cameraPrefs.end();
}

void saveCameraSettings() {
  cameraPrefs.begin("camera", false);  // Read-write mode
  
  cameraPrefs.putInt("frameSize", currentSettings.frameSize);
  cameraPrefs.putInt("quality", currentSettings.quality);
  cameraPrefs.putInt("brightness", currentSettings.brightness);
  cameraPrefs.putInt("contrast", currentSettings.contrast);
  cameraPrefs.putInt("saturation", currentSettings.saturation);
  cameraPrefs.putInt("fps", currentSettings.fps);
  cameraPrefs.putBool("vflip", currentSettings.vflip);
  cameraPrefs.putBool("hmirror", currentSettings.hmirror);
  cameraPrefs.putBool("streaming", currentSettings.streaming);
  
  cameraPrefs.end();
}

void initServerSettings() {
  initWiFiSettings();
  btPrefs.begin("bluetooth", false);
  
  // Load saved camera settings or use defaults
  loadCameraSettings();
}

void setSettingsPollInterval(unsigned long interval) {
  pollInterval = interval;
}

void applyCameraSettings(const CameraSettings& settings) {
  sensor_t* s = esp_camera_sensor_get();
  if (!s) {
    Serial.println("Failed to get camera sensor");
    return;
  }
  
  // Применяем настройки сенсора (всегда применяем, даже если значение не изменилось)
  s->set_framesize(s, (framesize_t)settings.frameSize);
  s->set_quality(s, settings.quality);
  s->set_brightness(s, settings.brightness);
  s->set_contrast(s, settings.contrast);
  s->set_saturation(s, settings.saturation);
  s->set_vflip(s, settings.vflip ? 1 : 0);
  s->set_hmirror(s, settings.hmirror ? 1 : 0);
  
  // Управление FPS через модуль стриминга
  setStreamFPS(settings.fps);
  
  // Управление стримингом
  if (settings.streaming && !currentSettings.streaming) {
    startStreaming();
  } else if (!settings.streaming && currentSettings.streaming) {
    stopStreaming();
  }
  
  currentSettings = settings;
  
  // Save settings to NVS for persistence
  saveCameraSettings();
}

CameraSettings getCurrentSettings() {
  return currentSettings;
}

static void processSettings(const String& json) {
  JsonDocument doc;
  DeserializationError error = deserializeJson(doc, json);
  
  if (error) {
    Serial.printf("JSON parse error: %s\n", error.c_str());
    return;
  }
  
  // Check for commands
  const char* command = doc["command"] | "";
  
  if (strcmp(command, "restart") == 0) {
    Serial.println("Restart command received");
    delay(100);
    ESP.restart();
    return;
  }
  
  // Handle WiFi settings
  if (doc["wifi"].is<JsonObject>()) {
    JsonObject wifi = doc["wifi"];
    if (wifi["ssid"].is<const char*>() && wifi["password"].is<const char*>()) {
      String newSSID = wifi["ssid"].as<String>();
      String newPassword = wifi["password"].as<String>();
      
      if (newSSID.length() > 0) {
        saveWiFiCredentials(newSSID, newPassword);
        Serial.println("WiFi credentials updated. Restarting...");
        delay(500);
        ESP.restart();
        return;
      }
    }
  }
  
  // Handle Bluetooth settings
  if (doc["bluetooth"].is<JsonObject>()) {
    JsonObject bt = doc["bluetooth"];
    if (bt["name"].is<const char*>()) {
      String btName = bt["name"].as<String>();
      if (btName.length() > 0 && btName.length() < 32) {
        btPrefs.putString("name", btName);
        Serial.println("Bluetooth name updated. Restart required.");
      }
    }
    if (bt["enabled"].is<bool>()) {
      bool enabled = bt["enabled"].as<bool>();
      btPrefs.putBool("enabled", enabled);
      Serial.printf("Bluetooth %s\n", enabled ? "enabled" : "disabled");
    }
  }
  
  // Handle camera settings
  CameraSettings newSettings = currentSettings;
  
  if (doc["frameSize"].is<int>()) {
    newSettings.frameSize = doc["frameSize"].as<int>();
  }
  if (doc["quality"].is<int>()) {
    newSettings.quality = doc["quality"].as<int>();
  }
  if (doc["brightness"].is<int>()) {
    newSettings.brightness = doc["brightness"].as<int>();
  }
  if (doc["contrast"].is<int>()) {
    newSettings.contrast = doc["contrast"].as<int>();
  }
  if (doc["saturation"].is<int>()) {
    newSettings.saturation = doc["saturation"].as<int>();
  }
  if (doc["fps"].is<int>()) {
    newSettings.fps = doc["fps"].as<int>();
  }
  if (doc["vflip"].is<bool>()) {
    newSettings.vflip = doc["vflip"].as<bool>();
  }
  if (doc["hmirror"].is<bool>()) {
    newSettings.hmirror = doc["hmirror"].as<bool>();
  }
  if (doc["streaming"].is<bool>()) {
    newSettings.streaming = doc["streaming"].as<bool>();
  }
  
  // Handle SD recording settings
  if (doc["recording"].is<JsonObject>()) {
    JsonObject rec = doc["recording"];
    if (rec["enabled"].is<bool>()) {
      bool recEnabled = rec["enabled"].as<bool>();
      setRecordingEnabled(recEnabled);
      if (recEnabled) {
        startRecording();
      } else {
        stopRecording();
      }
    }
    if (rec["interval"].is<int>()) {
      int interval = rec["interval"].as<int>();
      if (interval >= 5 && interval <= 60) {
        setRecordingInterval(interval);
      }
    }
    if (rec["clear"].is<bool>() && rec["clear"].as<bool>()) {
      clearAllRecordings();
    }
  }
  
  // Применяем только если что-то изменилось
  if (memcmp(&newSettings, &currentSettings, sizeof(CameraSettings)) != 0) {
    applyCameraSettings(newSettings);
  }
}

void handleServerSettings() {
  if (!isWiFiConnected()) return;
  
  // КРИТИЧНО: Пропускаем если уже идёт HTTP операция
  if (settingsBusy) return;
  
  unsigned long now = millis();
  if (now - lastPollTime < pollInterval) return;
  lastPollTime = now;
  
  // Пропускаем если идёт активная отправка видео (не мешаем стримингу)
  if (isStreaming()) {
    // Проверяем только если стриминг не слишком активен
    static unsigned long lastCheckTime = 0;
    if (now - lastCheckTime < 100) {
      return;  // Слишком частые кадры - откладываем проверку
    }
    lastCheckTime = now;
  }
  
  settingsBusy = true;  // Устанавливаем флаг занятости
  
  // Cache URLs on first call (use dynamic server host from NVS)
  if (!urlsCached) {
    String serverHost = getCurrentServerHost();
    settingsURL = String("http://") + serverHost + ":" + String(SERVER_PORT) + SETTINGS_PATH;
    statusURL = String("http://") + serverHost + ":" + String(SERVER_PORT) + STATUS_PATH;
    urlsCached = true;
  }
  
  HTTPClient http;
  http.setConnectTimeout(200);  // Уменьшили с 300 до 200
  http.setTimeout(400);  // Уменьшили с 500 до 400
  http.setReuse(false);  // Don't reuse connection for settings
  
  if (http.begin(settingsURL)) {
    http.addHeader("X-Device-ID", WiFi.macAddress());
    http.addHeader("X-Device-IP", getLocalIP());
    
    int httpCode = http.GET();
    
    if (httpCode == HTTP_CODE_OK) {
      String payload = http.getString();
      if (payload.length() > 2) {
        processSettings(payload);
      }
    }
    
    http.end();
  }
  
  settingsBusy = false;  // Снимаем флаг занятости
}

void sendStatusToServer() {
  if (!isWiFiConnected()) return;
  
  // КРИТИЧНО: Пропускаем если уже идёт HTTP операция
  if (settingsBusy) return;
  
  unsigned long now = millis();
  if (now - lastStatusTime < statusInterval) return;
  lastStatusTime = now;
  
  // Пропускаем если идёт активная отправка видео
  if (isStreaming()) {
    static unsigned long lastStatusCheckTime = 0;
    if (now - lastStatusCheckTime < 100) {
      return;  // Слишком частые кадры - откладываем отправку статуса
    }
    lastStatusCheckTime = now;
  }
  
  settingsBusy = true;  // Устанавливаем флаг занятости
  
  // Cache URLs on first call (use dynamic server host from NVS)
  if (!urlsCached) {
    String serverHost = getCurrentServerHost();
    settingsURL = String("http://") + serverHost + ":" + String(SERVER_PORT) + SETTINGS_PATH;
    statusURL = String("http://") + serverHost + ":" + String(SERVER_PORT) + STATUS_PATH;
    urlsCached = true;
  }
  
  JsonDocument doc;
  doc["device_id"] = WiFi.macAddress();
  doc["ip"] = getLocalIP();
  doc["streaming"] = isStreaming();
  doc["wifi_rssi"] = WiFi.RSSI();
  doc["uptime"] = millis() / 1000;
  doc["free_heap"] = ESP.getFreeHeap();
  doc["frames_sent"] = getFramesSent();
  doc["frames_failed"] = getFailedFrames();
  
  // SD card recording status
  JsonObject recording = doc["recording"].to<JsonObject>();
  recording["active"] = isRecording();
  recording["status"] = getRecordingStatus();
  
  SDCardInfo sdInfo = getSDCardInfo();
  if (sdInfo.mounted) {
    JsonObject sdcard = doc["sdcard"].to<JsonObject>();
    sdcard["mounted"] = true;
    sdcard["total_mb"] = sdInfo.totalMB;
    sdcard["used_mb"] = sdInfo.usedMB;
    sdcard["free_mb"] = sdInfo.freeMB;
    sdcard["file_count"] = sdInfo.fileCount;
  } else {
    doc["sdcard"]["mounted"] = false;
  }
  
  // Current camera settings
  JsonObject camera = doc["camera"].to<JsonObject>();
  camera["frameSize"] = currentSettings.frameSize;
  camera["quality"] = currentSettings.quality;
  camera["brightness"] = currentSettings.brightness;
  camera["contrast"] = currentSettings.contrast;
  camera["saturation"] = currentSettings.saturation;
  camera["fps"] = currentSettings.fps;
  camera["vflip"] = currentSettings.vflip;
  camera["hmirror"] = currentSettings.hmirror;
  
  String json;
  serializeJson(doc, json);
  
  HTTPClient http;
  http.setConnectTimeout(200);  // Уменьшили с 300 до 200
  http.setTimeout(400);  // Уменьшили с 500 до 400
  http.setReuse(false);
  
  if (http.begin(statusURL)) {
    http.addHeader("Content-Type", "application/json");
    http.POST(json);
    http.end();
  }
  
  settingsBusy = false;  // Снимаем флаг занятости
}

bool fetchInitialSettingsFromServer() {
  if (!isWiFiConnected()) {
    Serial.println("Cannot fetch settings: WiFi not connected");
    return false;
  }
  
  // Cache URLs if needed
  if (!urlsCached) {
    String serverHost = getCurrentServerHost();
    settingsURL = String("http://") + serverHost + ":" + String(SERVER_PORT) + SETTINGS_PATH;
    statusURL = String("http://") + serverHost + ":" + String(SERVER_PORT) + STATUS_PATH;
    urlsCached = true;
  }
  
  Serial.println("Fetching initial settings from server: " + settingsURL);
  
  HTTPClient http;
  http.setConnectTimeout(5000);  // 5 seconds timeout
  http.setTimeout(5000);
  http.setReuse(false);
  
  if (!http.begin(settingsURL)) {
    Serial.println("Failed to begin HTTP connection");
    return false;
  }
  
  int httpCode = http.GET();
  
  if (httpCode == HTTP_CODE_OK) {
    String payload = http.getString();
    
    // Process settings
    processSettings(payload);
    
    initialSettingsLoaded = true;
    http.end();
    return true;
  } else {
    Serial.printf("Failed to fetch settings, HTTP code: %d\n", httpCode);
    http.end();
    return false;
  }
}

bool areInitialSettingsLoaded() {
  return initialSettingsLoaded;
}

