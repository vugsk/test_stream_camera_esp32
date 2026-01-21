#include "sd_recorder.h"
#include "config.h"
#include <SD_MMC.h>
#include <FS.h>
#include <Preferences.h>

// ==================== Настройки записи ====================
static const int DEFAULT_RECORDING_INTERVAL = 10;  // Интервал записи в секундах
static const int MAX_FILES = 1000;                 // Максимальное количество файлов
static const unsigned long MIN_FREE_SPACE = 10 * 1024 * 1024;  // 10MB минимум свободного места
static const char* RECORD_DIR = "/records";        // Папка для записей
static const char* TEMP_SUFFIX = ".tmp";           // Суффикс для временных файлов

// ==================== NVS для настроек ====================
static Preferences recPrefs;

// ==================== Состояние модуля ====================
static bool sdCardPresent = false;
static bool sdCardWasPresent = false;  // Для отслеживания извлечения/вставки
static bool recordingEnabled = false;
static bool isCurrentlyRecording = false;
static int recordingInterval = DEFAULT_RECORDING_INTERVAL;
static unsigned long lastCardCheck = 0;
static const unsigned long CARD_CHECK_INTERVAL = 5000;  // Проверка карты каждые 5 секунд
static int cardCheckFailCount = 0;  // Счётчик неудачных проверок для debounce
static const int CARD_CHECK_FAIL_THRESHOLD = 2;  // Сколько раз подряд нет карты перед детектом извлечения

// Текущий файл записи
static File currentFile;
static String currentFilePath = "";
static String currentTempPath = "";
static unsigned long recordingStartTime = 0;
static unsigned long framesInCurrentFile = 0;
static unsigned long totalFramesRecorded = 0;
static unsigned long totalFilesCreated = 0;
static bool recordingBusy = false;  // Флаг для предотвращения блокировки при долгих операциях

// AVI параметры
static uint32_t aviMoviOffset = 0;  // Смещение до movi секции
static uint32_t aviTotalFrameSize = 0;  // Общий размер всех кадров
static uint16_t aviWidth = 1280;  // Ширина видео (будет определено из первого кадра)
static uint16_t aviHeight = 720;  // Высота видео

// Счетчик файлов
static int currentFileIndex = 0;
static int oldestFileIndex = 1;
static int newestFileIndex = 0;

// ==================== Вспомогательные функции ====================

// Формирование имени файла по индексу
static String getFileName(int index) {
  char name[20];
  snprintf(name, sizeof(name), "/%03d.avi", index);
  return String(RECORD_DIR) + name;
}

// Формирование имени временного файла по индексу
static String getTempFileName(int index) {
  char name[25];
  snprintf(name, sizeof(name), "/%03d.avi%s", index, TEMP_SUFFIX);
  return String(RECORD_DIR) + name;
}

// Проверка существования файла
static bool fileExists(const String& path) {
  return SD_MMC.exists(path);
}

// Удаление файла
static bool deleteFile(const String& path) {
  if (SD_MMC.exists(path)) {
    return SD_MMC.remove(path);
  }
  return true;
}

// Получить свободное место на SD карте (байты)
static unsigned long getFreeSpace() {
  return SD_MMC.totalBytes() - SD_MMC.usedBytes();
}

// Записать 32-битное значение в little-endian
static void write32LE(File& file, uint32_t value) {
  file.write((uint8_t)(value & 0xFF));
  file.write((uint8_t)((value >> 8) & 0xFF));
  file.write((uint8_t)((value >> 16) & 0xFF));
  file.write((uint8_t)((value >> 24) & 0xFF));
}

// Записать 16-битное значение в little-endian
static void write16LE(File& file, uint16_t value) {
  file.write((uint8_t)(value & 0xFF));
  file.write((uint8_t)((value >> 8) & 0xFF));
}

// Записать FourCC код
static void writeFourCC(File& file, const char* fourcc) {
  file.write((const uint8_t*)fourcc, 4);
}

// Создать AVI заголовок
static bool writeAVIHeader(File& file, uint16_t width, uint16_t height, uint32_t fps) {
  // RIFF header
  writeFourCC(file, "RIFF");
  write32LE(file, 0);  // Размер файла (будет обновлен позже)
  writeFourCC(file, "AVI ");
  
  // hdrl (header list)
  writeFourCC(file, "LIST");
  write32LE(file, 192);  // Размер hdrl
  writeFourCC(file, "hdrl");
  
  // avih (main AVI header)
  writeFourCC(file, "avih");
  write32LE(file, 56);  // Размер avih
  write32LE(file, 1000000 / fps);  // Микросекунд на кадр
  write32LE(file, 0);  // Максимальный размер потока
  write32LE(file, 0);  // Padding
  write32LE(file, 0x10);  // Флаги (AVIF_HASINDEX)
  write32LE(file, 0);  // Количество кадров (будет обновлено)
  write32LE(file, 0);  // Initial frames
  write32LE(file, 1);  // Количество потоков
  write32LE(file, 0);  // Suggested buffer size
  write32LE(file, width);
  write32LE(file, height);
  write32LE(file, 0);  // Reserved
  write32LE(file, 0);
  write32LE(file, 0);
  write32LE(file, 0);
  
  // strl (stream list)
  writeFourCC(file, "LIST");
  write32LE(file, 116);  // Размер strl
  writeFourCC(file, "strl");
  
  // strh (stream header)
  writeFourCC(file, "strh");
  write32LE(file, 56);  // Размер strh
  writeFourCC(file, "vids");  // Stream type: video
  writeFourCC(file, "MJPG");  // Codec: MJPEG
  write32LE(file, 0);  // Флаги
  write16LE(file, 0);  // Priority
  write16LE(file, 0);  // Language
  write32LE(file, 0);  // Initial frames
  write32LE(file, 1);  // Scale
  write32LE(file, fps);  // Rate (FPS)
  write32LE(file, 0);  // Start
  write32LE(file, 0);  // Length (кадры, будет обновлено)
  write32LE(file, 0);  // Suggested buffer size
  write32LE(file, 0);  // Quality
  write32LE(file, 0);  // Sample size
  write16LE(file, 0);  // Frame left
  write16LE(file, 0);  // Frame top
  write16LE(file, width);  // Frame right
  write16LE(file, height);  // Frame bottom
  
  // strf (stream format)
  writeFourCC(file, "strf");
  write32LE(file, 40);  // Размер BITMAPINFOHEADER
  write32LE(file, 40);  // Size of BITMAPINFOHEADER
  write32LE(file, width);
  write32LE(file, height);
  write16LE(file, 1);  // Planes
  write16LE(file, 24);  // Bit count
  writeFourCC(file, "MJPG");
  write32LE(file, width * height * 3);  // Image size
  write32LE(file, 0);  // X pixels per meter
  write32LE(file, 0);  // Y pixels per meter
  write32LE(file, 0);  // Colors used
  write32LE(file, 0);  // Important colors
  
  // movi (movie data)
  aviMoviOffset = file.position();
  writeFourCC(file, "LIST");
  write32LE(file, 4);  // Размер movi (будет обновлен)
  writeFourCC(file, "movi");
  
  return true;
}

// Обновить AVI заголовок с финальными значениями
static bool finalizeAVIHeader(File& file, uint32_t frameCount, uint32_t totalDataSize) {
  if (!file) return false;
  
  // Обновляем RIFF размер
  file.seek(4);
  write32LE(file, file.size() - 8);
  
  // Обновляем количество кадров в avih
  file.seek(48);
  write32LE(file, frameCount);
  
  // Обновляем длину в strh
  file.seek(140);
  write32LE(file, frameCount);
  
  // Обновляем размер movi
  file.seek(aviMoviOffset + 4);
  write32LE(file, totalDataSize + 4);
  
  return true;
}

// Найти следующий доступный индекс для нового файла
static int findNextFileIndex() {
  // Ищем первый свободный индекс или самый старый файл
  int nextIndex = newestFileIndex + 1;
  if (nextIndex > MAX_FILES) {
    nextIndex = 1;
  }
  return nextIndex;
}

// Удалить самый старый файл для освобождения места
static bool deleteOldestFile() {
  // Ищем самый старый файл
  for (int i = 0; i < MAX_FILES; i++) {
    int index = oldestFileIndex + i;
    if (index > MAX_FILES) {
      index = index - MAX_FILES;
    }
    
    String path = getFileName(index);
    if (fileExists(path)) {
      if (deleteFile(path)) {
        Serial.println("Deleted oldest file: " + path);
        oldestFileIndex = index + 1;
        if (oldestFileIndex > MAX_FILES) {
          oldestFileIndex = 1;
        }
        return true;
      }
    }
  }
  return false;
}

// Освободить место если нужно
static bool ensureFreeSpace() {
  int attempts = 0;
  while (getFreeSpace() < MIN_FREE_SPACE && attempts < 10) {
    if (!deleteOldestFile()) {
      Serial.println("Cannot free space on SD card");
      return false;
    }
    attempts++;
  }
  return getFreeSpace() >= MIN_FREE_SPACE;
}

// Удалить все временные файлы (незавершенные записи)
static void cleanupTempFiles() {
  File root = SD_MMC.open(RECORD_DIR);
  if (!root || !root.isDirectory()) {
    return;
  }
  
  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    if (name.endsWith(TEMP_SUFFIX)) {
      String path = String(RECORD_DIR) + "/" + name;
      file.close();
      SD_MMC.remove(path);
      Serial.println("Removed incomplete file: " + path);
    }
    file = root.openNextFile();
  }
  root.close();
}

// Сканировать существующие файлы для определения индексов
static void scanExistingFiles() {
  oldestFileIndex = MAX_FILES + 1;
  newestFileIndex = 0;
  
  File root = SD_MMC.open(RECORD_DIR);
  if (!root || !root.isDirectory()) {
    oldestFileIndex = 1;
    return;
  }
  
  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    // Парсим номер из имени файла (например "001.avi")
    if (name.endsWith(".avi") && !name.endsWith(TEMP_SUFFIX)) {
      int index = name.substring(0, 3).toInt();
      if (index > 0 && index <= MAX_FILES) {
        if (index < oldestFileIndex) {
          oldestFileIndex = index;
        }
        if (index > newestFileIndex) {
          newestFileIndex = index;
        }
      }
    }
    file = root.openNextFile();
  }
  root.close();
  
  // Если файлов нет
  if (oldestFileIndex > MAX_FILES) {
    oldestFileIndex = 1;
  }
  
  Serial.printf("SD files scan: oldest=%d, newest=%d\n", oldestFileIndex, newestFileIndex);
}

// Загрузка настроек записи из NVS
static void loadRecordingSettings() {
  recPrefs.begin("sdrec", true);  // RO mode
  recordingEnabled = recPrefs.getBool("enabled", SD_RECORDING_ENABLED);
  recordingInterval = recPrefs.getInt("interval", SD_RECORDING_INTERVAL);
  recPrefs.end();
  
  Serial.printf("Loaded recording settings: enabled=%d, interval=%d\n", 
                recordingEnabled, recordingInterval);
}

// Сохранение настроек записи в NVS
static void saveRecordingSettings() {
  recPrefs.begin("sdrec", false);  // RW mode
  recPrefs.putBool("enabled", recordingEnabled);
  recPrefs.putInt("interval", recordingInterval);
  recPrefs.end();
}

// Попытка переинициализации SD карты (для горячей вставки)
static bool reinitSDCard() {
  // НЕ вызываем SD_MMC.end() чтобы не мешать камере и стримингу
  // Просто пробуем заново примонтировать
  if (!SD_MMC.begin("/sdcard", true)) {
    return false;
  }
  
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    return false;
  }
  
  return true;
}

// ==================== Публичные функции ====================

bool initSDRecorder() {
  Serial.println("Initializing SD card recorder...");
  
  // Загружаем настройки из NVS
  loadRecordingSettings();
  
  // Инициализация SD_MMC (использует 1-bit режим для освобождения пина flash)
  // Для AI-Thinker ESP32-CAM используется 1-bit режим
  
  // Пробуем инициализировать несколько раз (SD карта может быть не готова)
  int attempts = 0;
  bool mounted = false;
  while (attempts < 3 && !mounted) {
    attempts++;
    Serial.printf("SD card init attempt %d/3...\n", attempts);
    
    if (SD_MMC.begin("/sdcard", true)) {  // true = 1-bit mode
      mounted = true;
      break;
    }
    
    if (attempts < 3) {
      delay(500);  // Пауза перед повторной попыткой
    }
  }
  
  if (!mounted) {
    Serial.println("========================================");
    Serial.println("SD CARD INIT FAILED!");
    Serial.println("Possible reasons:");
    Serial.println("1. No SD card inserted");
    Serial.println("2. Insufficient power supply (use 5V/2A)");
    Serial.println("3. Missing pull-up resistors (10k on CMD/DATA)");
    Serial.println("4. Bad contact or dirty SD card");
    Serial.println("5. Incompatible SD card (use Class 10, ≤32GB)");
    Serial.println("========================================");
    sdCardPresent = false;
    sdCardWasPresent = false;
    return false;
  }
  
  uint8_t cardType = SD_MMC.cardType();
  if (cardType == CARD_NONE) {
    Serial.println("No SD card detected (cardType = NONE)");
    sdCardPresent = false;
    return false;
  }
  
  // Информация о карте
  Serial.print("SD Card Type: ");
  switch (cardType) {
    case CARD_MMC:  Serial.println("MMC"); break;
    case CARD_SD:   Serial.println("SDSC"); break;
    case CARD_SDHC: Serial.println("SDHC"); break;
    default:        Serial.println("UNKNOWN"); break;
  }
  
  Serial.printf("SD Card Size: %lluMB\n", SD_MMC.totalBytes() / (1024 * 1024));
  Serial.printf("SD Card Free: %lluMB\n", getFreeSpace() / (1024 * 1024));
  
  sdCardPresent = true;
  
  // Создаем папку для записей
  if (!SD_MMC.exists(RECORD_DIR)) {
    if (!SD_MMC.mkdir(RECORD_DIR)) {
      Serial.println("Failed to create records directory");
      return false;
    }
    Serial.println("Created records directory");
  }
  
  // Удаляем незавершенные записи
  cleanupTempFiles();
  
  // Сканируем существующие файлы
  scanExistingFiles();
  
  sdCardWasPresent = true;
  Serial.println("SD card recorder initialized");
  return true;
}

bool isSDCardPresent() {
  if (!sdCardPresent) {
    return false;
  }
  
  // Проверяем доступность карты
  if (SD_MMC.cardType() == CARD_NONE) {
    sdCardPresent = false;
    return false;
  }
  
  return true;
}

// Периодическая проверка SD карты (вызывать из loop)
void handleSDRecorder() {
  unsigned long now = millis();
  if (now - lastCardCheck < CARD_CHECK_INTERVAL) {
    return;
  }
  lastCardCheck = now;
  
  // Если карта была смонтирована, проверяем её наличие
  if (sdCardPresent && sdCardWasPresent) {
    uint8_t cardType = SD_MMC.cardType();
    if (cardType == CARD_NONE) {
      cardCheckFailCount++;
      // Debounce: детектим извлечение только после нескольких неудач подряд
      if (cardCheckFailCount >= CARD_CHECK_FAIL_THRESHOLD) {
        Serial.println("SD card removed");
        if (isCurrentlyRecording) {
          // Принудительно останавливаем запись (файл потерян)
          isCurrentlyRecording = false;
          currentFile.close();
          currentFilePath = "";
          currentTempPath = "";
          framesInCurrentFile = 0;
          aviTotalFrameSize = 0;
        }
        sdCardPresent = false;
        sdCardWasPresent = false;
        cardCheckFailCount = 0;
      }
    } else {
      // Карта на месте - сбрасываем счётчик
      cardCheckFailCount = 0;
    }
  }
  
  // Если карта не смонтирована, проверяем не вставили ли её
  // Проверяем реже чтобы не спамить ошибками SDMMC
  if (!sdCardPresent && !sdCardWasPresent) {
    // Пробуем переинициализировать только раз в N проверок
    static int reinitAttempts = 0;
    reinitAttempts++;
    if (reinitAttempts >= 3) {  // Каждые 15 секунд (5 сек * 3)
      reinitAttempts = 0;
      if (reinitSDCard()) {
        Serial.println("SD card inserted - reinitializing...");
        sdCardPresent = true;
        sdCardWasPresent = true;
        cardCheckFailCount = 0;
        
        // Создаем папку если нужно
        if (!SD_MMC.exists(RECORD_DIR)) {
          SD_MMC.mkdir(RECORD_DIR);
        }
        
        // Очищаем временные файлы и сканируем
        cleanupTempFiles();
        scanExistingFiles();
        
        Serial.printf("SD card ready: %lluMB free\n", getFreeSpace() / (1024 * 1024));
        
        // Автоматически начинаем запись если включена
        if (recordingEnabled) {
          Serial.println("Auto-starting recording...");
          startRecording();
        }
      }
    }
  }
}

bool startRecording() {
  if (!isSDCardPresent()) {
    return false;
  }
  
  if (!recordingEnabled) {
    return false;
  }
  
  if (isCurrentlyRecording) {
    return true;  // Уже записываем
  }
  
  recordingBusy = true;  // Устанавливаем флаг блокировки
  
  // Проверяем свободное место
  if (!ensureFreeSpace()) {
    recordingBusy = false;
    return false;
  }
  
  // Определяем индекс нового файла
  currentFileIndex = findNextFileIndex();
  
  // Создаем временный файл
  currentTempPath = getTempFileName(currentFileIndex);
  currentFilePath = getFileName(currentFileIndex);
  
  // Удаляем старые файлы с этим индексом если есть
  deleteFile(currentFilePath);
  deleteFile(currentTempPath);
  
  // Открываем файл для записи
  currentFile = SD_MMC.open(currentTempPath, FILE_WRITE);
  if (!currentFile) {
    recordingBusy = false;
    return false;
  }
  
  // Записываем AVI заголовок (разрешение будет определено из первого кадра)
  // Используем 30 FPS как среднее значение
  writeAVIHeader(currentFile, aviWidth, aviHeight, 30);
  
  isCurrentlyRecording = true;
  recordingStartTime = millis();
  framesInCurrentFile = 0;
  aviTotalFrameSize = 0;
  recordingBusy = false;  // Снимаем флаг блокировки
  
  Serial.println("Recording started: " + currentTempPath);
  return true;
}

void stopRecording() {
  if (!isCurrentlyRecording) {
    return;
  }
  
  recordingBusy = true;  // Устанавливаем флаг блокировки
  isCurrentlyRecording = false;
  
  // Финализируем AVI заголовок
  if (currentFile && framesInCurrentFile > 0) {
    finalizeAVIHeader(currentFile, framesInCurrentFile, aviTotalFrameSize);
    currentFile.close();  // Убрали flush() - он медленный
    
    // Переименовываем из .tmp в .avi (это быстрая операция)
    if (SD_MMC.rename(currentTempPath, currentFilePath)) {
      // Обновляем индекс самого нового файла
      newestFileIndex = currentFileIndex;
      totalFilesCreated++;
    } else {
      deleteFile(currentTempPath);
    }
  } else {
    // Нет кадров - закрываем и удаляем пустой файл
    if (currentFile) {
      currentFile.close();
    }
    deleteFile(currentTempPath);
  }
  
  currentFilePath = "";
  currentTempPath = "";
  framesInCurrentFile = 0;
  aviTotalFrameSize = 0;
  recordingBusy = false;  // Снимаем флаг блокировки
}

void recordFrame(uint8_t* jpegData, size_t jpegLen) {
  // Быстрый выход если запись выключена или карты нет
  if (!recordingEnabled || !isSDCardPresent()) {
    return;
  }
  
  // КРИТИЧНО: Пропускаем кадр если идёт долгая операция (start/stop)
  // Это предотвращает блокировку видеопотока
  if (recordingBusy) {
    return;
  }
  
  // Проверяем нужно ли начать новую запись
  if (!isCurrentlyRecording) {
    startRecording();  // Может установить recordingBusy
    return;  // Пропускаем этот кадр
  }
  
  // Проверяем время - если прошло больше интервала, завершаем текущую запись
  unsigned long elapsed = (millis() - recordingStartTime) / 1000;
  if (elapsed >= (unsigned long)recordingInterval) {
    stopRecording();  // Может установить recordingBusy
    return;  // Пропускаем этот кадр, начнём новую запись на следующем
  }
  
  // Записываем кадр в AVI формате (00dc chunk)
  if (currentFile) {
    // Записываем chunk ID "00dc" (compressed video)
    writeFourCC(currentFile, "00dc");
    
    // Записываем размер JPEG данных
    write32LE(currentFile, jpegLen);
    
    // Записываем JPEG данные (быстрая операция в буфер)
    size_t written = currentFile.write(jpegData, jpegLen);
    if (written != jpegLen) {
      // Тихо пропускаем ошибку чтобы не блокировать поток
      stopRecording();
      return;
    }
    
    // Padding для выравнивания на 2 байта
    if (jpegLen % 2 != 0) {
      currentFile.write((uint8_t)0);
      aviTotalFrameSize += jpegLen + 8 + 1;  // chunk header + data + padding
    } else {
      aviTotalFrameSize += jpegLen + 8;  // chunk header + data
    }
    
    framesInCurrentFile++;
    totalFramesRecorded++;
    
    // УБРАЛИ flush() - он блокирует выполнение на ~50-100мс
    // Файловая система сама синхронизирует данные периодически
  }
}

bool isRecording() {
  return isCurrentlyRecording;
}

String getRecordingStatus() {
  if (!sdCardPresent) {
    return "SD card not present";
  }
  
  if (!recordingEnabled) {
    return "Recording disabled";
  }
  
  if (isCurrentlyRecording) {
    unsigned long elapsed = (millis() - recordingStartTime) / 1000;
    return "Recording: " + String(elapsed) + "s / " + String(recordingInterval) + "s, " +
           String(framesInCurrentFile) + " frames";
  }
  
  return "Standby";
}

SDCardInfo getSDCardInfo() {
  SDCardInfo info;
  info.mounted = sdCardPresent;
  info.totalMB = 0;
  info.usedMB = 0;
  info.freeMB = 0;
  info.fileCount = 0;
  
  if (!sdCardPresent) {
    return info;
  }
  
  info.totalMB = SD_MMC.totalBytes() / (1024 * 1024);
  info.freeMB = getFreeSpace() / (1024 * 1024);
  info.usedMB = info.totalMB - info.freeMB;
  info.fileCount = (newestFileIndex >= oldestFileIndex) ? 
                   (newestFileIndex - oldestFileIndex + 1) : 0;
  
  return info;
}

String getSDCardInfoString() {
  SDCardInfo info = getSDCardInfo();
  
  if (!info.mounted) {
    return "SD: Not present";
  }
  
  return "SD: " + String(info.usedMB) + "/" + String(info.totalMB) + "MB, Files: " + 
         String(info.fileCount);
}

void setRecordingEnabled(bool enabled) {
  if (recordingEnabled == enabled) {
    return;
  }
  
  recordingEnabled = enabled;
  saveRecordingSettings();  // Сохраняем в NVS
  
  if (!enabled && isCurrentlyRecording) {
    stopRecording();
  }
  
  Serial.println(enabled ? "Recording enabled" : "Recording disabled");
}

bool isRecordingEnabled() {
  return recordingEnabled;
}

void setRecordingInterval(int seconds) {
  if (seconds < 5) seconds = 5;
  if (seconds > 300) seconds = 300;
  
  if (recordingInterval == seconds) {
    return;
  }
  
  recordingInterval = seconds;
  saveRecordingSettings();  // Сохраняем в NVS
  Serial.printf("Recording interval set to %d seconds\n", recordingInterval);
}

int getRecordingInterval() {
  return recordingInterval;
}

bool clearAllRecordings() {
  if (!sdCardPresent) {
    return false;
  }
  
  // Останавливаем запись
  stopRecording();
  
  // Удаляем все файлы в папке записей
  File root = SD_MMC.open(RECORD_DIR);
  if (!root || !root.isDirectory()) {
    return false;
  }
  
  int deleted = 0;
  File file = root.openNextFile();
  while (file) {
    String name = file.name();
    String path = String(RECORD_DIR) + "/" + name;
    file.close();
    if (SD_MMC.remove(path)) {
      deleted++;
    }
    file = root.openNextFile();
  }
  root.close();
  
  // Сбрасываем индексы
  oldestFileIndex = 1;
  newestFileIndex = 0;
  currentFileIndex = 0;
  
  Serial.printf("Cleared %d recordings\n", deleted);
  return true;
}
