// bluetooth / transmision implementation
#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H
#include <Arduino.h>
#include <BLEDevice.h>
#include <BLEUtils.h>
#include <BLEServer.h>

// Define UUIDs (Generate your own for a real project)
#define SERVICE_UUID        "4fafc201-1fb5-459e-8fcc-c5c9c331914b"
#define CHARACTERISTIC_UUID "beb5483e-36e1-4688-b7f5-ea07361b26a8"

// Function prototypes
void initBLE();
void updateBLEValue(int value);

#endif
