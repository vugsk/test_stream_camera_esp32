#include "server_settings.h"
#include "config.h"
#include "camera.h"
#include "wifi_client.h"
#include "wifi_settings.h"
#include "stream_client.h"
#include <WiFi.h>
#include <HTTPClient.h>
#include <ArduinoJson.h>
#include "esp_camera.h"
#include <Preferences.h>

static unsigned long lastPollTime = 0;
static unsigned long pollInterval = 5000;  // 5 sec - less network load
static Preferences btPrefs;
static Preferences cameraPrefs;

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
  
  Serial.println("Camera settings loaded from NVS (or defaults)");
  Serial.printf("  Frame size: %d, Quality: %d, FPS: %d\n", 
                currentSettings.frameSize, currentSettings.quality, currentSettings.fps);
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
  
  Serial.println("Camera settings saved to NVS");
}

void initServerSettings() {
  initWiFiSettings();
  btPrefs.begin("bluetooth", false);
  
  // Load saved camera settings or use defaults
  loadCameraSettings();
  
  Serial.println("Server settings module initialized");
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
  
  // Применяем настройки сенсора
  if (settings.frameSize != currentSettings.frameSize) {
    s->set_framesize(s, (framesize_t)settings.frameSize);
    Serial.printf("Frame size set to: %d\n", settings.frameSize);
  }
  
  if (settings.quality != currentSettings.quality) {
    s->set_quality(s, settings.quality);
    Serial.printf("Quality set to: %d\n", settings.quality);
  }
  
  if (settings.brightness != currentSettings.brightness) {
    s->set_brightness(s, settings.brightness);
  }
  
  if (settings.contrast != currentSettings.contrast) {
    s->set_contrast(s, settings.contrast);
  }
  
  if (settings.saturation != currentSettings.saturation) {
    s->set_saturation(s, settings.saturation);
  }
  
  if (settings.vflip != currentSettings.vflip) {
    s->set_vflip(s, settings.vflip ? 1 : 0);
  }
  
  if (settings.hmirror != currentSettings.hmirror) {
    s->set_hmirror(s, settings.hmirror ? 1 : 0);
  }
  
  // Управление FPS через модуль стриминга
  if (settings.fps != currentSettings.fps) {
    setStreamFPS(settings.fps);
    Serial.printf("FPS set to: %d\n", settings.fps);
  }
  
  // Управление стримингом
  if (settings.streaming != currentSettings.streaming) {
    if (settings.streaming) {
      startStreaming();
      Serial.println("Streaming enabled by server");
    } else {
      stopStreaming();
      Serial.println("Streaming disabled by server");
    }
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
  
  // Применяем только если что-то изменилось
  if (memcmp(&newSettings, &currentSettings, sizeof(CameraSettings)) != 0) {
    applyCameraSettings(newSettings);
  }
}

void handleServerSettings() {
  if (!isWiFiConnected()) return;
  
  unsigned long now = millis();
  if (now - lastPollTime < pollInterval) return;
  lastPollTime = now;
  
  // Cache URLs on first call
  if (!urlsCached) {
    settingsURL = String("http://") + SERVER_HOST + ":" + String(SERVER_PORT) + SETTINGS_PATH;
    statusURL = String("http://") + SERVER_HOST + ":" + String(SERVER_PORT) + STATUS_PATH;
    urlsCached = true;
  }
  
  HTTPClient http;
  http.setConnectTimeout(300);
  http.setTimeout(500);
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
}

void sendStatusToServer() {
  if (!isWiFiConnected()) return;
  
  // Cache URLs on first call
  if (!urlsCached) {
    settingsURL = String("http://") + SERVER_HOST + ":" + String(SERVER_PORT) + SETTINGS_PATH;
    statusURL = String("http://") + SERVER_HOST + ":" + String(SERVER_PORT) + STATUS_PATH;
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
  http.setConnectTimeout(300);
  http.setTimeout(500);
  http.setReuse(false);
  
  if (http.begin(statusURL)) {
    http.addHeader("Content-Type", "application/json");
    http.POST(json);
    http.end();
  }
}
