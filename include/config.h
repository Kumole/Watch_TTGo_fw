#ifndef CONFIG_H
#define CONFIG_H

#include <Arduino.h>

// => Hardware select
//#define LILYGO_WATCH_2020_V2             //To use T-Watch2020 V2, please uncomment this line
#define LILYGO_WATCH_2020_V3           //To use T-Watch2020 V2, please uncomment this line
#include <LilyGoWatch.h>

#include "BluetoothSerial.h"
#include <stdio.h>
#include <stdlib.h>
#include <cmath>
#include "utils.h"

static constexpr const char *WATCH_BLUETOOTH_NAME = "Hiking Watch";
static constexpr uint32_t WATCH_BT_PROTOCOL_VERSION = 2;

#endif
