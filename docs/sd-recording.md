# Запись видео на SD карту

## Обзор

ESP32-CAM поддерживает автоматическую запись видео на MicroSD карту в формате AVI (MJPEG). Запись работает параллельно с видеопотоком и не влияет на производительность стриминга.

---

## Технические характеристики

### Формат записи

| Параметр | Значение |
|----------|----------|
| **Контейнер** | AVI (Audio Video Interleave) |
| **Видеокодек** | MJPEG (Motion JPEG) |
| **Совместимость** | VLC, Windows Media Player, QuickTime, все видеоплееры |
| **Структура** | RIFF AVI с полными заголовками |

### Параметры по умолчанию

```cpp
#define SD_RECORDING_ENABLED false      // Запись выключена по умолчанию
#define SD_RECORDING_INTERVAL 10        // Интервал записи 10 секунд
```

---

## Структура файловой системы

### Расположение файлов

```
/sdcard/
└── records/              # Папка для записей (создаётся автоматически)
    ├── 001.avi          # Первая запись
    ├── 002.avi          # Вторая запись
    ├── 003.avi          # Третья запись
    └── ...
    └── 999.avi          # Максимум 999 файлов
```

### Именование файлов

- **Формат**: `XXX.avi` где XXX - номер от 001 до 999
- **Последовательность**: Файлы нумеруются последовательно
- **Циклическая перезапись**: При достижении 999 начинается с 001 (старые удаляются)

---

## Механизм безопасной записи

### Временные файлы

Во время записи файл создаётся с суффиксом `.tmp`:

```
001.avi.tmp  →  001.avi  (после успешного завершения)
002.avi.tmp  →  002.avi
```

### Защита от повреждения данных

1. **Запись в .tmp файл** - данные пишутся во временный файл
2. **Финализация AVI заголовков** - обновляются счётчики кадров и размеры
3. **Атомарное переименование** - `rename()` гарантирует целостность
4. **Очистка при загрузке** - все `.tmp` файлы удаляются при старте

**Результат**: Даже при внезапном отключении питания или извлечении карты, файлы `.avi` остаются валидными.

---

## Горячая замена SD карты

### Обнаружение извлечения

```cpp
// Проверка каждые 5 секунд
if (SD_MMC.cardType() == CARD_NONE) {
  cardCheckFailCount++;
  if (cardCheckFailCount >= 2) {
    // Карта извлечена
    stopRecording();
    sdCardPresent = false;
  }
}
```

**Debounce защита**: Карта считается извлечённой только после 2 неудачных проверок подряд.

### Обнаружение вставки

```cpp
// Попытка переинициализации каждые 15 секунд
if (reinitAttempts >= 3) {
  if (reinitSDCard()) {
    // Карта вставлена
    cleanupTempFiles();
    scanExistingFiles();
    if (recordingEnabled) {
      startRecording();  // Автозапуск записи
    }
  }
}
```

**Автовозобновление**: Если запись была включена до извлечения, она автоматически возобновляется после вставки карты.

---

## Управление свободным пространством

### Автоматическая очистка

```cpp
#define MIN_FREE_SPACE (10 * 1024 * 1024)  // 10 MB минимум
```

Перед началом новой записи проверяется свободное место:

```cpp
if (freeSpace < MIN_FREE_SPACE) {
  deleteOldestFile();  // Удаляем самый старый файл
}
```

### Алгоритм удаления

1. **Поиск старейшего файла** - файл с минимальным номером
2. **Удаление** - `SD_MMC.remove(path)`
3. **Обновление индексов** - `oldestFileIndex++`
4. **Повтор** - пока не освободится достаточно места

---

## Формат AVI файла

### Структура контейнера

```
┌─────────────────────────────────────┐
│ RIFF Header                         │
│ ├─ "RIFF"                          │
│ ├─ File Size (4 bytes)             │
│ └─ "AVI "                          │
├─────────────────────────────────────┤
│ hdrl (Header List)                  │
│ ├─ avih (Main AVI Header)          │
│ │  ├─ Frame rate: 30 FPS          │
│ │  ├─ Frame count: N              │
│ │  └─ Resolution: 1280x720        │
│ └─ strl (Stream List)              │
│    ├─ strh (Stream Header)         │
│    │  └─ Codec: MJPEG              │
│    └─ strf (Stream Format)         │
│       └─ BITMAPINFOHEADER          │
├─────────────────────────────────────┤
│ movi (Movie Data)                   │
│ ├─ 00dc (Frame 1 Chunk)           │
│ │  ├─ Chunk Size (4 bytes)        │
│ │  └─ JPEG Data                    │
│ ├─ 00dc (Frame 2 Chunk)           │
│ │  ├─ Chunk Size (4 bytes)        │
│ │  └─ JPEG Data                    │
│ └─ ...                             │
└─────────────────────────────────────┘
```

### Финализация заголовков

При остановке записи обновляются:

1. **RIFF Size** (offset 4): Общий размер файла - 8
2. **Frame Count in avih** (offset 48): Количество записанных кадров
3. **Length in strh** (offset 140): Длина потока в кадрах
4. **movi Size** (offset aviMoviOffset+4): Размер секции данных

---

## Производительность

### Неблокирующая запись

```cpp
void recordFrame(uint8_t* jpegData, size_t jpegLen) {
  // Быстрый выход если busy
  if (recordingBusy) return;
  
  // Записываем в буфер (1-5 мс)
  currentFile.write(jpegData, jpegLen);
  
  // НЕТ flush() - ФС синхронизирует автоматически
}
```

### Измерения производительности

| Операция | Время | Блокировка видео |
|----------|-------|------------------|
| Запись кадра (буфер) | 1-5 мс | ❌ Нет |
| Создание нового файла | 50-200 мс | ❌ Нет (кадр пропускается) |
| Финализация файла | 100-300 мс | ❌ Нет (кадр пропускается) |
| Удаление старого файла | 50-150 мс | ❌ Нет (в начале записи) |

**Видеопоток работает со стабильными 30-60 FPS независимо от записи на SD карту.**

---

## API управления

### Инициализация

```cpp
bool initSDRecorder();
```

Вызывается один раз в `setup()`. Выполняет:
- Монтирование SD_MMC в 1-bit режиме
- Создание папки `/records`
- Удаление временных `.tmp` файлов
- Сканирование существующих файлов
- Загрузку настроек из NVS

### Основные функции

```cpp
// Начать запись (если включена)
bool startRecording();

// Остановить текущую запись
void stopRecording();

// Записать кадр (вызывается автоматически из stream_client)
void recordFrame(uint8_t* jpegData, size_t jpegLen);

// Проверка состояния
bool isRecording();
String getRecordingStatus();
```

### Настройки

```cpp
// Включить/выключить запись (сохраняется в NVS)
void setRecordingEnabled(bool enabled);
bool isRecordingEnabled();

// Установить интервал записи в секундах (5-300)
void setRecordingInterval(int seconds);
int getRecordingInterval();
```

### Информация о SD карте

```cpp
// Получить структуру с данными
SDCardInfo getSDCardInfo();

struct SDCardInfo {
  bool mounted;        // Карта смонтирована
  uint32_t totalMB;    // Общий объём в MB
  uint32_t usedMB;     // Занято в MB
  uint32_t freeMB;     // Свободно в MB
  int fileCount;       // Количество записей
};
```

### Обслуживание

```cpp
// Горячая замена карты (вызывать из loop)
void handleSDRecorder();

// Очистить все записи
bool clearAllRecordings();

// Проверка наличия карты
bool isSDCardPresent();
```

---

## Удалённое управление через сервер

### Запрос настроек (GET /api/camera/settings)

```json
{
  "recording": {
    "enabled": true,
    "interval": 10,
    "clear": false
  }
}
```

| Параметр | Тип | Описание |
|----------|-----|----------|
| `enabled` | bool | Включить/выключить запись |
| `interval` | int | Интервал записи в секундах (5-300) |
| `clear` | bool | Очистить все записи (одноразовое действие) |

### Отправка статуса (POST /api/camera/status)

```json
{
  "recording": {
    "active": true,
    "status": "Recording: 5s / 10s, 150 frames"
  },
  "sdcard": {
    "mounted": true,
    "total_mb": 7640,
    "used_mb": 2340,
    "free_mb": 5300,
    "file_count": 25
  }
}
```

---

## Конфигурация в config.h

```cpp
// ==================== Настройки записи на SD карту ====================
#define SD_RECORDING_ENABLED true       // Включить запись по умолчанию
#define SD_RECORDING_INTERVAL 10        // Интервал записи в секундах
```

**Примечание**: Настройки сохраняются в NVS при изменении через сервер. При следующей загрузке используются сохранённые значения, а не из config.h.

---

## Хранение настроек в NVS

### Пространство имён: `sdrec`

| Ключ | Тип | Значение по умолчанию |
|------|-----|-----------------------|
| `enabled` | bool | `SD_RECORDING_ENABLED` |
| `interval` | int | `SD_RECORDING_INTERVAL` |

### Автоматическое сохранение

```cpp
void setRecordingEnabled(bool enabled) {
  recordingEnabled = enabled;
  
  recPrefs.begin("sdrec", false);  // RW mode
  recPrefs.putBool("enabled", enabled);
  recPrefs.end();
}
```

### Загрузка при старте

```cpp
void loadRecordingSettings() {
  recPrefs.begin("sdrec", true);  // RO mode
  recordingEnabled = recPrefs.getBool("enabled", SD_RECORDING_ENABLED);
  recordingInterval = recPrefs.getInt("interval", SD_RECORDING_INTERVAL);
  recPrefs.end();
}
```

---

## Примеры использования

### Пример 1: Базовая запись

```cpp
void setup() {
  // Инициализация
  initSDRecorder();
  
  // Включить запись с интервалом 15 секунд
  setRecordingInterval(15);
  setRecordingEnabled(true);
}

void loop() {
  // Обработка горячей замены карты
  handleSDRecorder();
  
  // Запись происходит автоматически в recordFrame()
  // который вызывается из stream_client.cpp
}
```

### Пример 2: Проверка статуса

```cpp
// Получить текущий статус
String status = getRecordingStatus();
// "Recording: 5s / 10s, 150 frames"
// или "SD card not present"
// или "Recording disabled"

// Получить информацию о карте
SDCardInfo info = getSDCardInfo();
if (info.mounted) {
  Serial.printf("SD: %d/%d MB, %d files\n", 
                info.usedMB, info.totalMB, info.fileCount);
}
```

### Пример 3: Ручное управление

```cpp
// Включить запись вручную
if (isSDCardPresent()) {
  setRecordingEnabled(true);
  startRecording();
}

// Остановить запись
stopRecording();
setRecordingEnabled(false);

// Очистить все записи
clearAllRecordings();
```

---

## Диагностика проблем

### SD карта не определяется

**Симптомы**: `SD card mount failed or not present`

**Решения**:
1. Проверьте совместимость карты (SDHC до 32GB работает лучше всего)
2. Убедитесь что карта отформатирована в FAT32
3. Проверьте контакты карты
4. Попробуйте другую карту

### Запись не начинается

**Симптомы**: `isRecording()` возвращает `false`

**Проверки**:
```cpp
// 1. Карта смонтирована?
if (!isSDCardPresent()) {
  Serial.println("No SD card");
}

// 2. Запись включена?
if (!isRecordingEnabled()) {
  Serial.println("Recording disabled");
  setRecordingEnabled(true);
}

// 3. Достаточно места?
SDCardInfo info = getSDCardInfo();
if (info.freeMB < 10) {
  Serial.println("Not enough space");
  clearAllRecordings();
}
```

### Файлы повреждены

**Симптомы**: Файлы `.avi` не воспроизводятся

**Причины**:
1. Питание отключено во время записи → файл остался как `.tmp`
2. Карта извлечена без остановки записи → заголовки не финализированы

**Решение**: Используйте только файлы без суффикса `.tmp`. Временные файлы автоматически удаляются при следующей загрузке.

### Медленная запись

**Симптомы**: Пропуск кадров, низкий FPS

**Причины**:
1. Медленная SD карта (класс ниже Class 10)
2. Фрагментация файловой системы

**Решение**:
1. Используйте SD карту Class 10 или UHS-I
2. Периодически форматируйте карту
3. Проверьте `recordingBusy` - должен быть `false` большую часть времени

---

## Технические ограничения

| Параметр | Ограничение |
|----------|-------------|
| Максимум файлов | 999 (001.avi - 999.avi) |
| Минимальный интервал | 5 секунд |
| Максимальный интервал | 300 секунд (5 минут) |
| Минимальное свободное место | 10 MB |
| Режим SD_MMC | 1-bit (для совместимости с flash LED) |
| Максимальный размер файла | Ограничен только свободным местом |

---

## Рекомендации

### Выбор SD карты

✅ **Рекомендуется**:
- Класс: Class 10 или UHS-I
- Объём: 8-32 GB (SDHC)
- Бренд: SanDisk, Samsung, Kingston

❌ **Не рекомендуется**:
- Карты Class 4 и ниже (слишком медленные)
- SDXC карты >32GB (проблемы совместимости)
- Неизвестные бренды

### Оптимальные настройки

```cpp
// Для длительной записи (24/7)
#define SD_RECORDING_INTERVAL 30    // 30 секунд - меньше нагрузка на карту

// Для коротких клипов
#define SD_RECORDING_INTERVAL 5     // 5 секунд - максимум деталей

// Баланс (по умолчанию)
#define SD_RECORDING_INTERVAL 10    // 10 секунд
```

### Продление срока службы карты

1. **Используйте циклическую запись** - автоматическая перезапись старых файлов
2. **Избегайте частых старт/стоп** - один длинный файл лучше чем много коротких
3. **Периодическое форматирование** - раз в месяц полностью форматируйте карту
4. **Мониторинг ошибок** - следите за `getRecordingStatus()`

---

## Интеграция с другими модулями

### stream_client.cpp

```cpp
void sendFrame() {
  camera_fb_t* fb = esp_camera_fb_get();
  
  // Записываем на SD карту (неблокирующе)
  if (isRecordingEnabled() && isSDCardPresent()) {
    recordFrame(fb->buf, fb->len);
  }
  
  // Отправляем на сервер
  sendFrameData(fb);
  
  esp_camera_fb_return(fb);
}
```

### server_settings.cpp

```cpp
// Обработка настроек записи
if (doc["recording"]["enabled"].is<bool>()) {
  bool enabled = doc["recording"]["enabled"];
  setRecordingEnabled(enabled);
  if (enabled) startRecording();
  else stopRecording();
}

if (doc["recording"]["interval"].is<int>()) {
  int interval = doc["recording"]["interval"];
  setRecordingInterval(interval);
}
```

### main.cpp

```cpp
void loop() {
  // Приоритет 1: Видеопоток
  updateStreaming();
  
  // Приоритет 2: Горячая замена SD карты
  handleSDRecorder();
  
  // Приоритет 3: Настройки
  handleServerSettings();
}
```

---

## Заключение

Система записи на SD карту спроектирована с упором на:

1. **Надёжность** - защита от повреждения данных
2. **Производительность** - не влияет на видеопоток
3. **Удобство** - горячая замена карты, автоматическое управление
4. **Совместимость** - стандартный AVI формат

Для получения дополнительной информации см.:
- [API документация](api.md)
- [Архитектура проекта](architecture.md)
- [Устранение неполадок](troubleshooting.md)
