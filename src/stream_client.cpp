#include "stream_client.h"
#include "config.h"
#include "camera.h"
#include "wifi_client.h"
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

// Минимальный интервал между переподключениями (мс)
static const unsigned long RECONNECT_INTERVAL = 1000;

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
  Serial.println("Streaming initialized");
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
    return true;
  }
  
  // Ограничиваем частоту переподключений
  unsigned long now = millis();
  if (now - lastReconnect < RECONNECT_INTERVAL) {
    return false;
  }
  lastReconnect = now;
  
  clientConnected = false;
  client.stop();
  delay(10);  // Небольшая пауза перед переподключением
  
  // Подключаемся
  client.setTimeout(1000);
  
  if (client.connect(SERVER_HOST, SERVER_PORT)) {
    clientConnected = true;
    client.setNoDelay(true);  // Отключаем алгоритм Нэйгла для минимальной задержки
    Serial.println("Connected to server");
    return true;
  }
  
  Serial.println("Failed to connect to server");
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
  
  // Пробуем подключиться сразу
  ensureConnected();
  
  Serial.printf("Streaming started to %s:%d%s\n", SERVER_HOST, SERVER_PORT, STREAM_PATH);
  return true;
}

void stopStreaming() {
  streamingEnabled = false;
  client.stop();
  clientConnected = false;
  Serial.println("Streaming stopped");
}

// Fast frame sending via raw socket
static inline bool sendFrameData(camera_fb_t* fb) {
  // Check connection is alive
  if (!client.connected()) {
    return false;
  }
  
  // Clear incoming buffer
  int clearCount = 0;
  while (client.available() && clearCount < 1000) {
    client.read();
    clearCount++;
  }
  
  // Build HTTP header
  int headerLen = snprintf(httpHeader, sizeof(httpHeader), HEADER_TEMPLATE,
    STREAM_PATH, SERVER_HOST, SERVER_PORT, fb->len, framesSent);
  
  // Send header
  size_t sent = client.write((uint8_t*)httpHeader, headerLen);
  if (sent != headerLen) {
    Serial.println("Failed to send header");
    return false;
  }
  
  // Send data in larger chunks for HD frames (faster transfer)
  const size_t CHUNK_SIZE = 8192;  // 8KB chunks for better performance
  size_t offset = 0;
  
  while (offset < fb->len) {
    size_t toSend = min(CHUNK_SIZE, fb->len - offset);
    sent = client.write(fb->buf + offset, toSend);
    
    if (sent != toSend) {
      Serial.printf("Failed to send data at offset %d\n", offset);
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
  
  // Отправляем
  if (sendFrameData(fb)) {
    framesSent++;
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
