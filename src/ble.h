#ifndef BLE_HANDLER_H
#define BLE_HANDLER_H

#include <Arduino.h>

void bleInit(const char *deviceName);
void bleProcess();
bool bleIsClientConnected();
String bleTakePendingCommand();
void bleSendCommand(const String &command, const String &payload = "");

#endif
