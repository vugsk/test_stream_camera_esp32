#include "stream_client.h"
#include "config.h"
#include "camera.h"
#include "wifi_client.h"
#include "wifi_settings.h"
#include "sd_recorder.h"
#include <WiFi.h>

static bool streamingEnabled = false;
static unsigned long lastFrameTime = 0;
static unsigned long frameInterval = 1000 / STREAM_FPS;
static unsigned long framesSent = 0;
static unsigned long failedFrames = 0;
static unsigned long streamStartTime = 0;
static unsigned long lastReconnect = 0;
static WiFiClient client;
static bool clientConnected = false;

// Динамический адрес сервера (загружается из NVS)
static String serverHost = "";

// Счетчик неудачных подключений к серверу
static int serverConnectionFailures = 0;
static const int MAX_SERVER_CONNECTION_FAILURES = 5;

// Минимальный интервал между переподключениями (мс)
static const unsigned long RECONNECT_INTERVAL = 3000;

// Кэшированные данные для HTTP запроса (не пересоздаём каждый раз)
static char httpHeader[256];
static const char* HEADER_TEMPLATE = 
  "POST %s HTTP/1.1\r\n"
  "Host: %s:%d\r\n"
  "Content-Type: image/jpeg\r\n"
  "Content-Length: %d\r\n"
  "Connection: keep-alive\r\n"
  "X-Frame: %lu\r\n"
  "\r\n";

void initStreaming() {
  frameInterval = 1000 / STREAM_FPS;
  streamingEnabled = false;
  framesSent = 0;
  failedFrames = 0;
  clientConnected = false;
  serverConnectionFailures = 0;
  
  // Загружаем адрес сервера из NVS
  serverHost = getCurrentServerHost();
}

void setStreamFPS(int fps) {
  if (fps < 1) fps = 1;
  if (fps > 60) fps = 60;
  frameInterval = 1000 / fps;
}

int getStreamFPS() {
  return 1000 / frameInterval;
}

// Подключение к серверу с persistent connection
static bool ensureConnected() {
  if (client.connected()) {
    // Сбрасываем только если уже подключены (не накапливаем ошибки если есть соединение)
    if (clientConnected) {
      serverConnectionFailures = 0;
    }
    return true;
  }
  
  // Если достигли лимита ошибок - не пытаемся подключаться
  if (serverConnectionFailures >= MAX_SERVER_CONNECTION_FAILURES) {
    return false;
  }
  
  // Ограничиваем частоту переподключений
  unsigned long now = millis();
  if (now - lastReconnect < RECONNECT_INTERVAL) {
    return false;
  }
  lastReconnect = now;
  
  clientConnected = false;
  if (client.connected()) {
    client.stop();
  }
  delay(100);
  
  // Подключаемся
  client.setTimeout(500);  // Уменьшен таймаут для быстрого обнаружения проблем
  
  if (client.connect(serverHost.c_str(), SERVER_PORT)) {
    clientConnected = true;
    client.setNoDelay(true);
    serverConnectionFailures = 0;  // Сбрасываем только при УСПЕШНОМ подключении
    return true;
  }
  
  serverConnectionFailures++;
  Serial.printf("Failed to connect to server (attempt %d/%d)\n", 
                serverConnectionFailures, MAX_SERVER_CONNECTION_FAILURES);
  return false;
}

bool startStreaming() {
  if (!isWiFiConnected()) {
    Serial.println("Cannot start streaming: WiFi not connected");
    return false;
  }
  
  streamingEnabled = true;
  framesSent = 0;
  failedFrames = 0;
  streamStartTime = millis();
  lastFrameTime = 0;
  // НЕ сбрасываем serverConnectionFailures - сохраняем счётчик для обнаружения проблем!
  
  // Пробуем подключиться сразу
  ensureConnected();
  
  Serial.println("Streaming started");
  return true;
}

void stopStreaming() {
  streamingEnabled = false;
  client.stop();
  clientConnected = false;
}

// Fast frame sending via raw socket
static inline bool sendFrameData(camera_fb_t* fb) {
  // Check connection is alive
  if (!client.connected()) {
    return false;
  }
  
  // Быстрая очистка входящего буфера (не более 100 байт)
  int clearCount = 0;
  while (client.available() && clearCount < 100) {
    client.read();
    clearCount++;
  }
  
  // Build HTTP header
  int headerLen = snprintf(httpHeader, sizeof(httpHeader), HEADER_TEMPLATE,
    STREAM_PATH, serverHost.c_str(), SERVER_PORT, fb->len, framesSent);
  
  // Send header
  size_t sent = client.write((uint8_t*)httpHeader, headerLen);
  if (sent != headerLen) {
    return false;
  }
  
  // Send data in larger chunks for HD frames (faster transfer)
  const size_t CHUNK_SIZE = 16384;  // 16KB chunks for maximum performance
  size_t offset = 0;
  
  while (offset < fb->len) {
    size_t toSend = min(CHUNK_SIZE, fb->len - offset);
    sent = client.write(fb->buf + offset, toSend);
    
    if (sent != toSend) {
      return false;
    }
    
    offset += sent;
  }
  
  return true;
}

void sendFrame() {
  if (!streamingEnabled || !isWiFiConnected()) return;
  
  unsigned long now = millis();
  if (now - lastFrameTime < frameInterval) return;
  lastFrameTime = now;
  
  // Проверяем/восстанавливаем соединение
  if (!ensureConnected()) {
    failedFrames++;
    return;
  }
  
  // Захватываем кадр
  camera_fb_t* fb = captureFrame();
  if (!fb) {
    failedFrames++;
    return;
  }
  
  // Записываем на SD карту (если включено)
  if (isRecordingEnabled() && isSDCardPresent()) {
    recordFrame(fb->buf, fb->len);
  }
  
  // Отправляем на сервер
  if (sendFrameData(fb)) {
    framesSent++;
    
    // Async чтение ответа сервера (не ждем полного ответа)
    // Это предотвращает переполнение TCP буфера
    if (client.available()) {
      while (client.available() && client.read() != -1) {
        // Быстро очищаем буфер
      }
    }
  } else {
    failedFrames++;
    clientConnected = false;
    client.stop();
  }
  
  // Освобождаем буфер камеры
  releaseFrame(fb);
}

void updateStreaming() {
  if (streamingEnabled) {
    sendFrame();
  }
}

bool isStreaming() {
  return streamingEnabled;
}

unsigned long getFramesSent() {
  return framesSent;
}

unsigned long getFailedFrames() {
  return failedFrames;
}

String getStreamingStatus() {
  if (streamingEnabled) {
    unsigned long elapsed = (millis() - streamStartTime) / 1000;
    float fps = elapsed > 0 ? (float)framesSent / elapsed : 0;
    return "Frames: " + String(framesSent) + " sent, " + String(failedFrames) + 
           " failed | " + String(fps, 1) + " FPS | " + String(elapsed) + "s";
  } else {
    return "Streaming: OFF";
  }
}

bool hasServerConnectionError() {
  return serverConnectionFailures >= MAX_SERVER_CONNECTION_FAILURES;
}

void resetServerConnectionErrors() {
  serverConnectionFailures = 0;
}

void setServerHost(const String& host) {
  if (host.length() > 0) {
    serverHost = host;
    saveServerHost(serverHost);
    stopStreaming();
  }
}

String getServerHost() {
  return serverHost;
}
