#ifndef WIFI_CLIENT_H
#define WIFI_CLIENT_H

#include <Arduino.h>
#include <WiFi.h>

// Инициализация WiFi подключения
bool initWiFi();

// Проверка подключения WiFi
bool isWiFiConnected();

// Проверка и переподключение WiFi
void checkWiFiConnection();

// Получить локальный IP адрес
String getLocalIP();

// Отключение WiFi
void disconnectWiFi();

#endif // WIFI_CLIENT_H
