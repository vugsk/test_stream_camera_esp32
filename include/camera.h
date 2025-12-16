#ifndef CAMERA_H
#define CAMERA_H

#include <Arduino.h>
#include "esp_camera.h"
#include "config.h"

// Инициализация камеры
bool initCamera();

// Получить кадр с камеры
camera_fb_t* captureFrame();

// Вернуть буфер кадра
void releaseFrame(camera_fb_t* fb);

#endif // CAMERA_H
