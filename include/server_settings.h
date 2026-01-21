#ifndef SERVER_SETTINGS_H
#define SERVER_SETTINGS_H

#include <Arduino.h>

/*
 * Server Settings Module
 * 
 * Получает настройки с веб-сервера в формате JSON:
 * {
 *   "command": "restart",  // Опционально: перезагрузка устройства
 *   "wifi": {
 *     "ssid": "MyNetwork",
 *     "password": "MyPassword"
 *   },
 *   "bluetooth": {
 *     "name": "ESP32-CAM-01",
 *     "enabled": true
 *   },
 *   "frameSize": 8,
 *   "quality": 12,
 *   "brightness": 0,
 *   "contrast": 0,
 *   "saturation": 0,
 *   "fps": 30,
 *   "vflip": false,
 *   "hmirror": false,
 *   "streaming": true
 * }
 */

// Camera settings structure
struct CameraSettings {
  int frameSize;      // Frame size: 5=QVGA(320x240), 8=VGA(640x480), 9=SVGA(800x600), 11=HD(1280x720), 13=UXGA(1600x1200)
  int quality;        // JPEG quality (10-63, lower = better)
  int brightness;     // Brightness (-2 to 2)
  int contrast;       // Contrast (-2 to 2)
  int saturation;     // Saturation (-2 to 2)
  int fps;            // Target FPS (1-60)
  bool vflip;         // Vertical flip
  bool hmirror;       // Horizontal mirror
  bool streaming;     // Streaming enabled
};

// Initialize settings module
void initServerSettings();

// Load camera settings from NVS (or use defaults from config.h)
void loadCameraSettings();

// Save camera settings to NVS
void saveCameraSettings();

// Проверка и обработка настроек с сервера (вызывать в loop)
void handleServerSettings();

// Применить настройки камеры
void applyCameraSettings(const CameraSettings& settings);

// Получить текущие настройки
CameraSettings getCurrentSettings();

// Отправить статус на сервер
void sendStatusToServer();

// Установить интервал опроса сервера (мс)
void setSettingsPollInterval(unsigned long interval);

// Загрузить настройки с сервера (синхронно, при первом подключении)
bool fetchInitialSettingsFromServer();

// Проверить, были ли загружены начальные настройки
bool areInitialSettingsLoaded();

// Проверить критическую ошибку загрузки начальных настроек (для переключения на Bluetooth)
bool hasInitialSettingsError();

// Сбросить счётчик ошибок загрузки настроек
void resetInitialSettingsAttempts();

#endif // SERVER_SETTINGS_H
