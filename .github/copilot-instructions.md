# Copilot Instructions for ESP32-CAM Video Stream Client

## Project Overview

This is an ESP32-CAM firmware project for video streaming to a web server. The project is built with PlatformIO using the Arduino framework.

## Key Technologies

- **Platform**: ESP32 (AI-Thinker ESP32-CAM board)
- **Framework**: Arduino
- **Build System**: PlatformIO
- **Libraries**: ArduinoJson, BluetoothSerial, WiFiClient, SD_MMC, Preferences

## Project Structure

```
src/
├── main.cpp           - Entry point, setup and loop
├── camera.cpp         - Camera initialization and configuration
├── wifi_client.cpp    - WiFi connection management
├── stream_client.cpp  - Video streaming to server
├── bluetooth_config.cpp - Bluetooth configuration
├── server_settings.cpp  - Remote settings management
├── wifi_settings.cpp    - WiFi credentials storage
├── sd_recorder.cpp      - SD card video recording

include/
├── config.h           - Global configuration constants
├── camera.h           - Camera module interface
├── wifi_client.h      - WiFi client interface
├── stream_client.h    - Stream client interface
├── bluetooth_config.h - Bluetooth config interface
├── server_settings.h  - Server settings interface
├── wifi_settings.h    - WiFi settings interface
├── sd_recorder.h      - SD recorder interface
```

## Coding Conventions

### General Rules
- Use Arduino-style C++ (no exceptions, limited STL)
- Prefer static variables for module-level state
- Use `Serial.println()` sparingly - only for important events
- All string literals should be in English

### Memory Management
- Use `PSRAM` for large buffers (frame buffers, JSON documents)
- Avoid dynamic allocations in loops
- Use `String` carefully - prefer stack buffers for temporary strings

### Module Pattern
Each module follows this pattern:
```cpp
// module.h
#ifndef MODULE_H
#define MODULE_H
#include <Arduino.h>

bool initModule();
void handleModule();  // Called in loop
// ... other functions
#endif

// module.cpp
#include "module.h"
#include "config.h"

static bool moduleState = false;  // Module-level state

bool initModule() {
    // Initialization
}

void handleModule() {
    // Processing
}
```

### Config Pattern
All configurable values go to `config.h`:
```cpp
// Default values (can be overridden by NVS)
#define SERVER_HOST "192.168.1.100"
#define SERVER_PORT 8081
#define DEFAULT_FPS 30
```

### NVS Storage Pattern
Using `Preferences` library for persistent storage:
```cpp
#include <Preferences.h>

static Preferences prefs;

void saveValue() {
    prefs.begin("namespace", false);  // RW mode
    prefs.putString("key", value);
    prefs.end();
}

String loadValue() {
    prefs.begin("namespace", true);   // RO mode
    String val = prefs.getString("key", "default");
    prefs.end();
    return val;
}
```

## Important Constraints

### WiFi/Bluetooth Conflict
ESP32 shares radio between WiFi and Bluetooth. Never use both simultaneously:
```cpp
// WRONG - will cause abort()
WiFi.begin(ssid, password);
SerialBT.begin("ESP32");

// CORRECT - stop one before starting other
SerialBT.end();
delay(100);
WiFi.begin(ssid, password);
```

### Camera Memory
- Use 1-bit SD_MMC mode to free GPIO for flash LED
- Always release frame buffer after use:
```cpp
camera_fb_t* fb = esp_camera_fb_get();
if (fb) {
    // Use frame
    esp_camera_fb_return(fb);
}
```

### SD Card Recording
- Files use `.tmp` suffix during recording
- Renamed to `.avi` on successful completion
- Incomplete files (`.tmp`) are deleted on boot
- AVI format: Standard MJPEG in AVI container (compatible with all video players)

## API Patterns

### Server Communication
- Settings endpoint: `GET /api/camera/settings`
- Status endpoint: `POST /api/camera/status`
- Stream endpoint: `POST /stream`

### JSON Structures
Settings from server:
```json
{
  "frameSize": 11,
  "quality": 15,
  "fps": 30,
  "streaming": true,
  "recording": {
    "enabled": true,
    "interval": 10
  }
}
```

Status to server:
```json
{
  "device_id": "MAC",
  "ip": "192.168.1.x",
  "streaming": true,
  "recording": {"active": true, "status": "..."},
  "sdcard": {"mounted": true, "total_mb": 7640}
}
```

## Common Tasks

### Adding New Setting
1. Add default to `config.h`
2. Add storage in `server_settings.cpp`
3. Add JSON parsing in `processSettings()`
4. Add to status JSON in `sendStatusToServer()`

### Adding New Module
1. Create `module.h` with interface
2. Create `module.cpp` with implementation
3. Add `initModule()` call to `setup()` in `main.cpp`
4. Add `handleModule()` call to `loop()` if needed

### Debugging
- Use `Serial.printf()` for formatted output
- Check free heap: `ESP.getFreeHeap()`
- Monitor via serial: `pio device monitor -b 115200`
