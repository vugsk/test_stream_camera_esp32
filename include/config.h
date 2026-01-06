#ifndef CONFIG_H
#define CONFIG_H

// ==================== Настройки Wi-Fi ====================
#define WIFI_SSID ""          // Имя вашей WiFi сети
#define WIFI_PASSWORD ""       // Пароль WiFi

// ==================== Настройки сервера ====================
#define SERVER_HOST ""      // IP вашего веб-сервера
#define SERVER_PORT 8081                 // Порт сервера
#define STREAM_PATH "/stream"            // Путь для отправки видео потока
#define SETTINGS_PATH "/api/camera"      // Путь для получения настроек
#define STATUS_PATH "/api/status"        // Путь для отправки статуса

// ==================== Пины камеры для AI-Thinker ESP32-CAM ====================
#define PWDN_GPIO_NUM     32
#define RESET_GPIO_NUM    -1
#define XCLK_GPIO_NUM      0
#define SIOD_GPIO_NUM     26
#define SIOC_GPIO_NUM     27
#define Y9_GPIO_NUM       35
#define Y8_GPIO_NUM       34
#define Y7_GPIO_NUM       39
#define Y6_GPIO_NUM       36
#define Y5_GPIO_NUM       21
#define Y4_GPIO_NUM       19
#define Y3_GPIO_NUM       18
#define Y2_GPIO_NUM        5
#define VSYNC_GPIO_NUM    25
#define HREF_GPIO_NUM     23
#define PCLK_GPIO_NUM     22

// ==================== Настройки стриминга ====================
#define STREAM_FPS 60                    // Target FPS
#define STREAM_QUALITY 15                // JPEG quality (10-63, lower=better, 15 good for HD@60fps)

#endif // CONFIG_H
