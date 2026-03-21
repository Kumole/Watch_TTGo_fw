#pragma once

#include <Arduino.h>

/**
 * Send a single newline-terminated protocol message to the hub.
 */
void sendBluetoothLine(const String &line);

/**
 * Send the next finished session waiting in storage.
 *
 * Returns true if one session was sent and an ACK is now expected.
 */
bool sendNextFinishedSessionBT();

/**
 * Handle one incoming Bluetooth protocol line.
 */
bool handleBluetoothCommand(const String &incoming);

/**
 * Poll the Classic Bluetooth serial interface for one line.
 */
void pollBluetooth();