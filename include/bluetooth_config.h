#ifndef BLUETOOTH_CONFIG_H
#define BLUETOOTH_CONFIG_H

#include <Arduino.h>

// Initialize Bluetooth for WiFi configuration
void initBluetoothConfig();

// Start Bluetooth and wait for WiFi credentials
void startBluetoothConfig();

// Stop Bluetooth
void stopBluetoothConfig();

// Check if Bluetooth is running
bool isBluetoothConfigActive();

// Check if new WiFi credentials were received
bool hasNewWiFiCredentials();

// Get received WiFi credentials
bool getReceivedCredentials(String& ssid, String& password);

// Handle Bluetooth events (call in loop)
void handleBluetoothConfig();

#endif // BLUETOOTH_CONFIG_H
