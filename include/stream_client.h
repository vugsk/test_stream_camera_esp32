#ifndef STREAM_CLIENT_H
#define STREAM_CLIENT_H

#include <Arduino.h>

// Инициализация стриминга
void initStreaming();

// Начать стриминг на сервер
bool startStreaming();

// Остановить стриминг
void stopStreaming();

// Отправить кадр (вызывать в loop)
void sendFrame();

// Обновление стриминга (вызывать в loop)
void updateStreaming();

// Проверка статуса стриминга
bool isStreaming();

// Установить целевой FPS
void setStreamFPS(int fps);

// Получить текущий FPS
int getStreamFPS();

// Получить количество отправленных кадров
unsigned long getFramesSent();

// Получить количество неудачных кадров
unsigned long getFailedFrames();

// Получить статус стриминга
String getStreamingStatus();

#endif // STREAM_CLIENT_H
