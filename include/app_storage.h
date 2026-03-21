#pragma once

#include "app_types.h"
#include <Arduino.h>
#include <FS.h>

/**
 * Normalize a LittleFS file name to always start with '/'.
 */
String normalizeFsPath(const String &name);

/**
 * Read a whole file into a String. Returns empty string on failure.
 */
String readFileAsString(fs::FS &fs, const char *path);

/**
 * Check whether a file name matches the session_<n>.json convention.
 */
bool parseSessionFileIndex(const String &rawName, int &idx);

/**
 * Read a key=value line from the runtime-state file.
 */
String getStateValue(const String &content, const char *key);

/**
 * Extract an unsigned integer field from a small JSON payload.
 */
uint32_t getJsonUintValue(const String &json, const char *key);

/**
 * Count stored sessions and update the next rotating session index.
 */
void updateSessionCount();

/**
 * Build the JSON payload used for per-session persistence and sync.
 */
String buildSessionPayload(const SessionRecord &record);

/**
 * Save one session file to LittleFS.
 */
void saveSessionData(int idx, const SessionRecord &record);

/**
 * Persist minimal runtime state needed to recover an unfinished session.
 */
void saveRuntimeState(bool sessionActive);

/**
 * Load and restore unfinished-session runtime state from LittleFS.
 */
void loadRuntimeState();

/**
 * Delete a session file by its logical session_id after ACK from the hub.
 */
bool deleteSessionById(const String &sessionId);