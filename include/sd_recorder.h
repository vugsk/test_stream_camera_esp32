#ifndef SD_RECORDER_H
#define SD_RECORDER_H

#include <Arduino.h>

/*
 * SD Card Recorder Module
 * 
 * Записывает видео на MicroSD карту интервалами по 10 секунд.
 * 
 * Особенности:
 * - MJPEG формат (последовательность JPEG кадров)
 * - Автоматическая перезапись старых файлов при заполнении
 * - Безопасное извлечение - файлы закрываются после каждого интервала
 * - Неполные записи автоматически удаляются
 * - Файлы нумеруются последовательно (001.mjpeg, 002.mjpeg, ...)
 * 
 * Использование:
 *   initSDRecorder();          // Инициализация
 *   startRecording();          // Начать запись
 *   recordFrame(fb);           // Записать кадр
 *   stopRecording();           // Остановить запись
 */

// Структура с информацией о SD карте
struct SDCardInfo {
  bool mounted;
  uint32_t totalMB;
  uint32_t usedMB;
  uint32_t freeMB;
  int fileCount;
};

// Инициализация SD карты
bool initSDRecorder();

// Проверка наличия SD карты
bool isSDCardPresent();

// Периодическая обработка (вызывать из loop для горячей вставки карты)
void handleSDRecorder();

// Начать запись
bool startRecording();

// Остановить запись
void stopRecording();

// Записать кадр (вызывать из loop)
void recordFrame(uint8_t* jpegData, size_t jpegLen);

// Проверка состояния записи
bool isRecording();

// Получить статус записи
String getRecordingStatus();

// Получить информацию о SD карте (структура с данными)
SDCardInfo getSDCardInfo();

// Получить информацию о SD карте (строка для логов)
String getSDCardInfoString();

// Включить/выключить запись через настройки сервера
void setRecordingEnabled(bool enabled);
bool isRecordingEnabled();

// Установить интервал записи (секунды)
void setRecordingInterval(int seconds);
int getRecordingInterval();

// Очистить все записи
bool clearAllRecordings();

#endif // SD_RECORDER_H
